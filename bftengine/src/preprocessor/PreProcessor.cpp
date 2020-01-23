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
  LOG_DEBUG(GL,
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
  const ReqId &seqNumberOfLastReply = replica_.seqNumberOfLastReplyToClient(clientId);
  if (seqNumberOfLastReply < reqSeqNum) {
    if (replica_.isCurrentPrimary()) {
      return handleClientPreProcessRequest(clientPreProcessReqMsg);
    } else {  // Not the current primary => pass it to the primary
      sendMsg(msg->body(), replica_.currentPrimary(), msg->type(), msg->size());
      LOG_DEBUG(GL,
                "Replica=" << replicaId_ << " sending ClientPreProcessRequestMsg reqSeqNum=" << reqSeqNum
                           << " to current primary");
      return;
    }
  }
  if (seqNumberOfLastReply == reqSeqNum) {
    LOG_DEBUG(GL,
              "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum
                         << " has already been executed - let replica complete handling");
    auto clientRequestMsg = make_unique<ClientRequestMsg>(
        senderId, 0, reqSeqNum, clientPreProcessReqMsg->requestLength(), clientPreProcessReqMsg->requestBuf());
    return incomingMsgsStorage_->pushExternalMsg(move(clientRequestMsg));
  }
  LOG_DEBUG(GL, "Replica=" << replicaId_ << " reqSeqNum=" << reqSeqNum << " is ignored: request is old");
}

void PreProcessor::registerClientPreProcessRequest(uint16_t clientId,
                                                   ReqId requestSeqNum,
                                                   ClientPreProcessReqMsgSharedPtr msg) {
  lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
  // Only one request is supported per client for now
  ongoingRequests_[clientId] = make_unique<RequestProcessingInfo>(
      numOfReplicas_, ReplicaConfigSingleton::GetInstance().GetFVal() + 1, requestSeqNum);
  ongoingRequests_[clientId]->saveClientPreProcessRequestMsg(msg);
}

void PreProcessor::releaseClientPreProcessRequest(uint16_t clientId) {
  lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
  ongoingRequests_.erase(clientId);
}

// Start client request handling - primary replica
void PreProcessor::handleClientPreProcessRequest(ClientPreProcessReqMsgSharedPtr clientPreProcessRequestMsg) {
  const uint16_t &clientId = clientPreProcessRequestMsg->clientProxyId();
  const ReqId &requestSeqNum = clientPreProcessRequestMsg->requestSeqNum();

  registerClientPreProcessRequest(clientId, requestSeqNum, clientPreProcessRequestMsg);
  sendPreProcessRequestToAllReplicas(clientPreProcessRequestMsg);

  // Primary replica: pre-process the request and calculate a hash of the result
  auto &replyEntry = preProcessResultBuffers_[getClientReplyBufferId(clientId)];
  const uint32_t actualResultBufLen = launchRequestPreProcessing(clientId,
                                                                 requestSeqNum,
                                                                 clientPreProcessRequestMsg->requestLength(),
                                                                 clientPreProcessRequestMsg->requestBuf(),
                                                                 (char *)replyEntry.data());
  lock_guard<recursive_mutex> lock(ongoingRequestsMutex_);
  ongoingRequests_[clientId]->savePrimaryPreProcessResult(replyEntry, actualResultBufLen);
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

  // TBD: Non-primary replica: pre-process the request and calculate a hash of the result
  char *replyBuffer = (char *)preProcessResultBuffers_[getClientReplyBufferId(preProcessReqMsg->clientId())].data();
  const uint32_t actualResultBufLen = launchRequestPreProcessing(preProcessReqMsg->clientId(),
                                                                 preProcessReqMsg->reqSeqNum(),
                                                                 preProcessReqMsg->requestLength(),
                                                                 preProcessReqMsg->requestBuf(),
                                                                 replyBuffer);
  // TBD calculate a hash of the result, sign and send back
  auto replyMsg = make_shared<PreProcessReplyMsg>(replicaId_,
                                                  preProcessReqMsg->clientId(),
                                                  preProcessReqMsg->reqSeqNum(),
                                                  preProcessReqMsg->viewNum(),
                                                  actualResultBufLen,
                                                  replyBuffer);
  sendMsg(replyMsg->body(), replica_.currentPrimary(), replyMsg->type(), replyMsg->size());
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

    // TBD replace by signatures collector logic and verify (f + 1) same hashes
    if (ongoingRequests_[preProcessReplyMsg->clientId()]->enoughRepliesReceived()) {
      LOG_DEBUG(GL, "Replica=" << replicaId_ << " All expected replies received for reqSeqNum=" << reqSeqNum);

      // TBD avoid copy of the message body, if possible
      auto clientRequestMsg = make_unique<ClientRequestMsg>(
          clientId, HAS_PRE_PROCESSED_FLAG, reqSeqNum, preProcessReplyMsg->replyLength(), preProcessReplyMsg->body());
      incomingMsgsStorage_->pushExternalMsg(move(clientRequestMsg));
      releaseClientPreProcessRequest(clientId);
      LOG_DEBUG(GL,
                "Replica=" << replicaId_ << " clientId=" << clientId << " reqSeqNum=" << reqSeqNum
                           << " pre-processing completed");
    }
  }
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

void PreProcessor::sendPreProcessRequestToAllReplicas(ClientPreProcessReqMsgSharedPtr clientPreProcessReqMsg) {
  PreProcessRequestMsgSharedPtr preProcessRequestMsg =
      make_shared<PreProcessRequestMsg>(replicaId_,
                                        clientPreProcessReqMsg->clientProxyId(),
                                        clientPreProcessReqMsg->requestSeqNum(),
                                        replica_.getCurrentView(),
                                        clientPreProcessReqMsg->requestLength(),
                                        clientPreProcessReqMsg->requestBuf());
  const set<ReplicaId> &idsOfPeerReplicas = replica_.getIdsOfPeerReplicas();
  for (auto destId : idsOfPeerReplicas) {
    if (destId != replicaId_) {
      auto *preProcessJob = new AsyncPreProcessJob(*this, preProcessRequestMsg, destId);
      LOG_DEBUG(GL,
                "Replica=" << replicaId_ << " reqSeqNum=" << preProcessRequestMsg->reqSeqNum()
                           << " from senderId=" << preProcessRequestMsg->senderId() << " sent to replica=" << destId
                           << " msg->size()=" << preProcessRequestMsg->size());
      threadPool_.add(preProcessJob);
    }
  }
}

//**************** Class AsyncPreProcessJob ****************//

AsyncPreProcessJob::AsyncPreProcessJob(PreProcessor &preProcessor, PreProcessRequestMsgSharedPtr msg, ReplicaId destId)
    : preProcessor_(preProcessor), msg_(msg), destId_(destId) {}

void AsyncPreProcessJob::execute() { preProcessor_.sendMsg(msg_->body(), destId_, msg_->type(), msg_->size()); }

void AsyncPreProcessJob::release() { delete this; }

}  // namespace preprocessor
