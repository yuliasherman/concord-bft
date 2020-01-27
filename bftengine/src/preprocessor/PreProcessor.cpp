// Concord
//
// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
//
// This product is licensed to you under the Apache 2.0 license (the "License"). You may not use this product except in
// compliance with the Apache 2.0 License.
//
// This product may include a number of subcomponents with separate copyright notices and license terms. Your use of
// these subcomponents is subject to the terms and conditions of the sub-component's license, as noted in the LICENSE
// file.

#include <messages/ClientReplyMsg.hpp>
#include "PreProcessor.hpp"
#include "Logger.hpp"
#include "MsgHandlersRegistrator.hpp"
#include "messages/ClientPreProcessRequestMsg.hpp"
#include "ReplicaConfig.hpp"

namespace preprocessor {

using namespace bftEngine;
using namespace concordUtils;
using namespace std;
using namespace std::placeholders;

//**************** Class PreProcessor ****************//

vector<unique_ptr<PreProcessor>> PreProcessor::preProcessors_;

void PreProcessor::registerMsgHandlers() {
  msgHandlersRegistrator_->registerMsgHandler(MsgCode::ClientPreProcessRequest,
                                              bind(&PreProcessor::onClientPreProcessRequestMsg, this, _1));
  msgHandlersRegistrator_->registerMsgHandler(MsgCode::PreProcessRequest,
                                              bind(&PreProcessor::onPreProcessRequestMsg, this, _1));
  msgHandlersRegistrator_->registerMsgHandler(MsgCode::PreProcessReply,
                                              bind(&PreProcessor::onPreProcessReplyMsg, this, _1));
}

PreProcessor::PreProcessor(shared_ptr<MsgsCommunicator> &msgsCommunicator,
                           shared_ptr<IncomingMsgsStorage> &incomingMsgsStorage,
                           shared_ptr<MsgHandlersRegistrator> &msgHandlersRegistrator,
                           IRequestsHandler &requestsHandler,
                           const InternalReplicaApi &replica)
    : requestsHandler_(requestsHandler),
      replica_(replica),
      replicaId_(ReplicaConfigSingleton::GetInstance().GetReplicaId()),
      maxReplyMsgSize_(ReplicaConfigSingleton::GetInstance().GetMaxReplyMessageSize() - sizeof(ClientReplyMsgHeader)),
      idsOfPeerReplicas_(replica.getIdsOfPeerReplicas()),
      numOfReplicas_(ReplicaConfigSingleton::GetInstance().GetNumOfReplicas()),
      numOfClients_(ReplicaConfigSingleton::GetInstance().GetNumOfClientProxies()) {
  msgsCommunicator_ = msgsCommunicator;
  incomingMsgsStorage_ = incomingMsgsStorage;
  msgHandlersRegistrator_ = msgHandlersRegistrator;
  registerMsgHandlers();
  threadPool_.start(numOfClients_);

  // Allocate a buffer for the pre-execution result per client
  for (auto id = 0; id < numOfClients_; id++)
    preProcessResultBuffers_.push_back(Sliver(new char[maxReplyMsgSize_], maxReplyMsgSize_));
}

PreProcessor::~PreProcessor() { threadPool_.stop(); }

ClientPreProcessReqMsgSharedPtr PreProcessor::convertMsgToClientPreProcessReq(MessageBase *&inMsg) {
  ClientPreProcessRequestMsg *outMsg = nullptr;
  if (!ClientPreProcessRequestMsg::ToActualMsgType(inMsg, outMsg)) {
    LOG_WARN(GL,
             "Replica=" << replicaId_ << " received invalid message type=" << inMsg->type()
                        << " from Node=" << inMsg->senderId());
    delete inMsg;
  }
  ClientPreProcessReqMsgSharedPtr resultMsg(outMsg);
  return resultMsg;
}

PreProcessRequestMsgSharedPtr PreProcessor::convertMsgToPreProcessRequest(MessageBase *&inMsg) {
  PreProcessRequestMsg *outMsg = nullptr;
  if (!PreProcessRequestMsg::ToActualMsgType(inMsg, outMsg)) {
    LOG_WARN(GL,
             "Replica=" << replicaId_ << " received invalid message type=" << inMsg->type()
                        << " from Node=" << inMsg->senderId());
    delete inMsg;
  }
  PreProcessRequestMsgSharedPtr resultMsg(outMsg);
  return resultMsg;
}

PreProcessReplyMsgSharedPtr PreProcessor::convertMsgToPreProcessReply(MessageBase *&inMsg) {
  PreProcessReplyMsg *outMsg = nullptr;
  if (!PreProcessReplyMsg::ToActualMsgType(inMsg, outMsg)) {
    LOG_WARN(GL,
             "Replica=" << replicaId_ << " received invalid message type=" << inMsg->type()
                        << " from Node=" << inMsg->senderId());
    delete inMsg;
  }
  PreProcessReplyMsgSharedPtr resultMsg(outMsg);
  return resultMsg;
}

bool PreProcessor::checkClientMsgCorrectness(ClientPreProcessReqMsgSharedPtr msg, ReqId reqSeqNum) const {
  if (replica_.isCollectingState()) {
    LOG_WARN(GL,
             "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum
                        << " is ignored because the replica is collecting missing state from other replicas");
    return false;
  }
  if (msg->isReadOnly()) {
    LOG_ERROR(
        GL, "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum << " is ignored because it is signed as read-only");
    return false;
  }
  const bool &invalidClient = !replica_.isValidClient(msg->clientProxyId());
  const bool &sentFromReplicaToNonPrimary = replica_.isIdOfReplica(msg->senderId()) && !replica_.isCurrentPrimary();
  if (invalidClient || sentFromReplicaToNonPrimary) {
    LOG_ERROR(GL,
              "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum << " is ignored as invalid: clientId="
                         << invalidClient << ", sentFromReplicaToNonPrimary=" << sentFromReplicaToNonPrimary);
    return false;
  }
  if (!replica_.currentViewIsActive()) {
    LOG_WARN(GL,
             "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum << " is ignored because current view is inactive");
    return false;
  }
  return true;
}

// If it's a primary replica - process, else - send to the primary
void PreProcessor::onClientPreProcessRequestMsg(MessageBase *msg) {
  ClientPreProcessReqMsgSharedPtr clientPreProcessReqMsg = convertMsgToClientPreProcessReq(msg);
  if (!clientPreProcessReqMsg) return;

  const NodeIdType &clientId = clientPreProcessReqMsg->clientProxyId();
  const NodeIdType &senderId = clientPreProcessReqMsg->senderId();
  const ReqId &reqSeqNum = clientPreProcessReqMsg->requestSeqNum();
  LOG_INFO(GL,
           "Replica=" << replicaId_ << " received ClientPreProcessRequestMsg reqSeqNum=" << reqSeqNum
                      << " from senderId=" << senderId);
  if (!checkClientMsgCorrectness(clientPreProcessReqMsg, reqSeqNum)) return;
  {
    lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
    auto clientEntry = ongoingRequests_.find(clientId);
    if (clientEntry != ongoingRequests_.end()) {
      LOG_WARN(GL,
               "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum
                          << " for clientId=" << clientPreProcessReqMsg->clientProxyId()
                          << " is ignored: previous client request=" << clientEntry->second->getReqSeqNum()
                          << " is in process");
      return;
    }
  }
  return handleClientPreProcessRequest(clientPreProcessReqMsg);
  //  const ReqId &seqNumberOfLastReply = replica_.seqNumberOfLastReplyToClient(clientId);
  //  if (seqNumberOfLastReply < reqSeqNum) {
  //    if (replica_.isCurrentPrimary()) {
  //      return handleClientPreProcessRequest(clientPreProcessReqMsg);
  //    } else {  // Not the current primary => pass it to the primary
  //      sendMsg(msg->body(), replica_.currentPrimary(), msg->type(), msg->size());
  //      LOG_INFO(GL,
  //                "Replica=" << replicaId_ << " sending ClientPreProcessRequestMsg reqSeqNum=" << reqSeqNum
  //                           << " to current primary");
  //      return;
  //    }
  //  }
  //  if (seqNumberOfLastReply == reqSeqNum) {
  //    LOG_INFO(GL,
  //              "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum
  //                         << " has already been executed - let replica complete handling");
  //    auto clientRequestMsg = make_unique<ClientRequestMsg>(
  //        senderId, 0, reqSeqNum, clientPreProcessReqMsg->requestLength(), clientPreProcessReqMsg->requestBuf());
  //    return incomingMsgsStorage_->pushExternalMsg(move(clientRequestMsg));
  //  }
  //  LOG_INFO(GL, "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum << " is ignored: request is old");
}

void PreProcessor::registerClientPreProcessRequest(uint16_t clientId,
                                                   ReqId requestSeqNum,
                                                   ClientPreProcessReqMsgSharedPtr msg) {
  lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
  // Only one request is supported per client for now
  ongoingRequests_[clientId] = make_unique<RequestProcessingInfo>(
      numOfReplicas_, ReplicaConfigSingleton::GetInstance().GetFVal() + 1, requestSeqNum);
  ongoingRequests_[clientId]->saveClientPreProcessRequestMsg(msg);
  LOG_DEBUG(GL, "clientId=" << clientId << " requestSeqNum=" << requestSeqNum << " REGISTERED");
}

void PreProcessor::releaseClientPreProcessRequest(uint16_t clientId, ReqId requestSeqNum) {
  lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
  ongoingRequests_.erase(clientId);
  LOG_DEBUG(GL, "clientId=" << clientId << " requestSeqNum=" << requestSeqNum << " RELEASED");
}

// Start client request handling - primary replica
void PreProcessor::handleClientPreProcessRequest(ClientPreProcessReqMsgSharedPtr clientPreProcessRequestMsg) {
  const uint16_t &clientId = clientPreProcessRequestMsg->clientProxyId();
  const ReqId &requestSeqNum = clientPreProcessRequestMsg->requestSeqNum();

  registerClientPreProcessRequest(clientId, requestSeqNum, clientPreProcessRequestMsg);
  sendPreProcessRequestToAllReplicas(clientPreProcessRequestMsg, 0);

  // Primary replica: pre-process the request and calculate a hash of the result
//  auto &replyEntry = preProcessResultBuffers_[getClientReplyBufferId(clientId)];
//  launchRequestPreProcessing(clientId,
//                             requestSeqNum,
//                             clientPreProcessRequestMsg->requestLength(),
//                             clientPreProcessRequestMsg->requestBuf(),
//                             (char *)replyEntry.data());
  // lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
  // ongoingRequests_[clientId]->savePrimaryPreProcessResult(replyEntry, actualResultBufLen);
  // sendClientReplyMsg(replicaId_, clientId, reqSeqNum);
  asyncPreProcessing(clientPreProcessRequestMsg);
  releaseClientPreProcessRequest(clientId, requestSeqNum);
}

uint32_t PreProcessor::launchRequestPreProcessing(
    uint16_t clientId, ReqId reqSeqNum, uint32_t reqLength, char *reqBuf, char *resultBuf) {
  uint32_t resultLen = 0;

  requestsHandler_.execute(
      clientId, reqSeqNum, PRE_PROCESS_FLAG, reqLength, reqBuf, maxReplyMsgSize_, resultBuf, resultLen);
  if (!resultLen)
    throw std::runtime_error("actualResultLength is 0 for clientId=" + to_string(clientId) +
                             ", requestSeqNum=" + to_string(reqSeqNum));
  return resultLen;
}

// Non-primary replica request handling
void PreProcessor::onPreProcessRequestMsg(MessageBase *msg) {
  PreProcessRequestMsgSharedPtr preProcessReqMsg = convertMsgToPreProcessRequest(msg);
  if (!preProcessReqMsg) return;

  LOG_DEBUG(GL,
            "Replica=" << replicaId_ << " received reqSeqNum=" << preProcessReqMsg->reqSeqNum()
                       << " from senderId=" << preProcessReqMsg->senderId());

  //  if (!preProcessReqMsg->sendReply()) {
  //    // TBD: Non-primary replica: pre-process the request and calculate a hash of the result
  //    char *replyBuffer = (char
  //    *)preProcessResultBuffers_[getClientReplyBufferId(preProcessReqMsg->clientId())].data(); const uint32_t
  //    actualResultBufLen = launchRequestPreProcessing(preProcessReqMsg->clientId(),
  //                                                                   preProcessReqMsg->reqSeqNum(),
  //                                                                   preProcessReqMsg->requestLength(),
  //                                                                   preProcessReqMsg->requestBuf(),
  //                                                                   replyBuffer);
  //    // TBD calculate a hash of the result, sign and send back
  //    auto replyMsg = make_shared<PreProcessReplyMsg>(replicaId_,
  //                                                    preProcessReqMsg->clientId(),
  //                                                    preProcessReqMsg->reqSeqNum(),
  //                                                    preProcessReqMsg->viewNum(),
  //                                                    actualResultBufLen,
  //                                                    replyBuffer);
  //    sendMsg(replyMsg->body(), replica_.currentPrimary(), replyMsg->type(), replyMsg->size());
  //  } else

  char *replyBuffer = (char *)preProcessResultBuffers_[getClientReplyBufferId(preProcessReqMsg->clientId())].data();
  launchRequestPreProcessing(preProcessReqMsg->clientId(),
                             preProcessReqMsg->reqSeqNum(),
                             preProcessReqMsg->requestLength(),
                             preProcessReqMsg->requestBuf(),
                             replyBuffer);
  sendClientReplyMsg(replicaId_, preProcessReqMsg->clientId(), preProcessReqMsg->reqSeqNum());
  LOG_DEBUG(GL,
            "Replica=" << replicaId_ << " sent reqSeqNum=" << preProcessReqMsg->reqSeqNum()
                       << " to the primary replica=" << replica_.currentPrimary());
}

// Primary replica handling
void PreProcessor::onPreProcessReplyMsg(MessageBase *msg) {
  PreProcessReplyMsgSharedPtr preProcessReplyMsg = convertMsgToPreProcessReply(msg);
  if (!preProcessReplyMsg) return;

  const SeqNum &reqSeqNum = preProcessReplyMsg->reqSeqNum();
  const NodeIdType &senderId = preProcessReplyMsg->senderId();
  const uint16_t &clientId = preProcessReplyMsg->clientId();
  bool enoughRepliesReceived = false;
  {
    lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
    auto clientEntry = ongoingRequests_.find(clientId);
    if ((clientEntry == ongoingRequests_.end()) || (clientEntry->second->getReqSeqNum() != reqSeqNum)) {
      LOG_DEBUG(GL,
                "Replica=" << replicaId_ << " Request with reqSeqNum=" << reqSeqNum
                           << " received from replica=" << senderId << " clientId=" << clientId
                           << " is ignored as no ongoing request from this client and given sequence number found");
      return;
    }
    ongoingRequests_[preProcessReplyMsg->clientId()]->savePreProcessReplyMsg(senderId, preProcessReplyMsg);
    enoughRepliesReceived = ongoingRequests_[preProcessReplyMsg->clientId()]->enoughRepliesReceived();
  }
  // TBD replace by signatures collector logic and verify (f + 1) same hashes
  if (enoughRepliesReceived) {
    LOG_DEBUG(GL, "Replica=" << replicaId_ << " All expected replies received for reqSeqNum=" << reqSeqNum);

    // TBD avoid copy of the message body, if possible
    auto clientRequestMsg = make_unique<ClientRequestMsg>(
        clientId, HAS_PRE_PROCESSED_FLAG, reqSeqNum, preProcessReplyMsg->replyLength(), preProcessReplyMsg->body());

    // TBD: REMOVE
    sendPreProcessRequestToAllReplicas(
        ongoingRequests_[preProcessReplyMsg->clientId()]->getClientPreProcessRequestMsg(), 1);

    // TBD: revive it
    // incomingMsgsStorage_->pushExternalMsg(move(clientRequestMsg));
    releaseClientPreProcessRequest(clientId, reqSeqNum);
    sendClientReplyMsg(replicaId_, clientId, reqSeqNum);
    LOG_DEBUG(GL,
              "Replica=" << replicaId_ << " clientId=" << clientId << " reqSeqNum=" << reqSeqNum
                         << " pre-processing completed");
  }
}

void PreProcessor::sendClientReplyMsg(ReplicaId senderId, NodeIdType clientId, SeqNum reqSeqNum) {
  const uint32_t replyBufSize = 50;
  char *replyBuf = new char[replyBufSize];
  memset(replyBuf, '5', replyBufSize);
  auto *replyMsg = new ClientReplyMsg(senderId, reqSeqNum, replyBuf, replyBufSize);
  replyMsg->setPrimaryId(replicaId_);
  sendMsg(replyMsg->body(), clientId, MsgCode::ClientReply, replyMsg->size());
  LOG_DEBUG(GL,
            "Sender=" << replyMsg->senderId() << " sent reply message to clientId=" << clientId
                      << " replyLength=" << replyMsg->replyLength() << " message size=" << replyMsg->size()
                      << " reqSeqNum=" << replyMsg->reqSeqNum()
                      << " currentPrimaryId=" << replyMsg->currentPrimaryId());
  delete replyMsg;
}

void PreProcessor::addNewPreProcessor(shared_ptr<MsgsCommunicator> &msgsCommunicator,
                                      shared_ptr<IncomingMsgsStorage> &incomingMsgsStorage,
                                      shared_ptr<MsgHandlersRegistrator> &msgHandlersRegistrator,
                                      bftEngine::IRequestsHandler &requestsHandler,
                                      InternalReplicaApi &replica) {
  preProcessors_.push_back(make_unique<PreProcessor>(
      msgsCommunicator, incomingMsgsStorage, msgHandlersRegistrator, requestsHandler, replica));
}

void PreProcessor::sendMsg(char *msg, NodeIdType dest, uint16_t msgType, MsgSize msgSize) {
  int errorCode = msgsCommunicator_->sendAsyncMessage(dest, msg, msgSize);
  if (errorCode != 0) {
    LOG_ERROR(GL,
              "Replica=" << replicaId_ << " sendAsyncMessage returned error=" << errorCode
                         << " for message type=" << msgType);
  }
}

void PreProcessor::sendPreProcessRequestToAllReplicas(ClientPreProcessReqMsgSharedPtr clientPreProcessReqMsg,
                                                      uint8_t sendReply) {
  PreProcessRequestMsgSharedPtr preProcessRequestMsg =
      make_shared<PreProcessRequestMsg>(replicaId_,
                                        clientPreProcessReqMsg->clientProxyId(),
                                        sendReply,
                                        clientPreProcessReqMsg->requestSeqNum(),
                                        replica_.getCurrentView(),
                                        clientPreProcessReqMsg->requestLength(),
                                        clientPreProcessReqMsg->requestBuf());
  const set<ReplicaId> &idsOfPeerReplicas = replica_.getIdsOfPeerReplicas();
  for (auto destId : idsOfPeerReplicas) {
    if (destId != replicaId_) {
      auto *preProcessJob = new AsyncSendPreProcessJob(*this, preProcessRequestMsg, destId);
      LOG_DEBUG(GL,
                "Replica=" << replicaId_ << " reqSeqNum=" << preProcessRequestMsg->reqSeqNum()
                           << " from senderId=" << preProcessRequestMsg->senderId() << " sent to replica=" << destId
                           << " msg->size()=" << preProcessRequestMsg->size());
      threadPool_.add(preProcessJob);
    }
  }
}

void PreProcessor::asyncPreProcessing(ClientPreProcessReqMsgSharedPtr preProcessReqMsg) {
  char *replyBuffer =
      (char *)preProcessResultBuffers_[getClientReplyBufferId(preProcessReqMsg->clientProxyId())].data();
  auto *preProcessJob = new AsyncPreProcessJob(*this, preProcessReqMsg, replyBuffer);
  threadPool_.add(preProcessJob);
}

AsyncPreProcessJob::AsyncPreProcessJob(PreProcessor &preProcessor,
                                       ClientPreProcessReqMsgSharedPtr msg,
                                       char *replyBuffer)
    : preProcessor_(preProcessor), msg_(msg), replyBuffer_(replyBuffer) {}

void AsyncPreProcessJob::execute() {
  preProcessor_.launchRequestPreProcessing(
      msg_->clientProxyId(), msg_->requestSeqNum(), msg_->requestLength(), msg_->requestBuf(), replyBuffer_);
  preProcessor_.sendClientReplyMsg(0, msg_->clientProxyId(), msg_->requestSeqNum());
}

void AsyncPreProcessJob::release() { delete this; }

//**************** Class AsyncPreProcessJob ****************//

AsyncSendPreProcessJob::AsyncSendPreProcessJob(PreProcessor &preProcessor,
                                               PreProcessRequestMsgSharedPtr msg,
                                               ReplicaId destId)
    : preProcessor_(preProcessor), msg_(msg), destId_(destId) {}

void AsyncSendPreProcessJob::execute() { preProcessor_.sendMsg(msg_->body(), destId_, msg_->type(), msg_->size()); }

void AsyncSendPreProcessJob::release() { delete this; }

}  // namespace preprocessor
