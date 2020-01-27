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

#pragma once

#include "MsgsCommunicator.hpp"
#include "MsgHandlersRegistrator.hpp"
#include "InternalReplicaApi.hpp"
#include "messages/ClientPreProcessRequestMsg.hpp"
#include "messages/PreProcessRequestMsg.hpp"
#include "SimpleThreadPool.hpp"
#include "Replica.hpp"
#include "RequestProcessingInfo.hpp"

namespace preprocessor {

class AsyncSendPreProcessJob;

//**************** Class PreProcessor ****************//

// This class is responsible for the coordination of pre-execution activities on both - primary and non-primary
// replica types. It handles client pre-execution requests, pre-processing requests, and replies.
// On primary replica - it collects pre-execution result hashes from other replicas and decides whether to continue
// or to fail request processing.

class PreProcessor {
 public:
  PreProcessor(std::shared_ptr<MsgsCommunicator> &msgsCommunicator,
               std::shared_ptr<IncomingMsgsStorage> &incomingMsgsStorage,
               std::shared_ptr<MsgHandlersRegistrator> &msgHandlersRegistrator,
               bftEngine::IRequestsHandler &requestsHandler,
               const InternalReplicaApi &replica);

  ~PreProcessor();

  static void addNewPreProcessor(std::shared_ptr<MsgsCommunicator> &msgsCommunicator,
                                 std::shared_ptr<IncomingMsgsStorage> &incomingMsgsStorage,
                                 std::shared_ptr<MsgHandlersRegistrator> &msgHandlersRegistrator,
                                 bftEngine::IRequestsHandler &requestsHandler,
                                 InternalReplicaApi &replica);

 private:
  friend class AsyncSendPreProcessJob;
  friend class AsyncPreProcessJob;

  ClientPreProcessReqMsgSharedPtr convertMsgToClientPreProcessReq(MessageBase *&inMsg);
  PreProcessRequestMsgSharedPtr convertMsgToPreProcessRequest(MessageBase *&inMsg);
  PreProcessReplyMsgSharedPtr convertMsgToPreProcessReply(MessageBase *&inMsg);

  void onClientPreProcessRequestMsg(MessageBase *msg);
  void onPreProcessRequestMsg(MessageBase *msg);
  void onPreProcessReplyMsg(MessageBase *msg);
  void registerMsgHandlers();
  bool checkClientMsgCorrectness(ClientPreProcessReqMsgSharedPtr msg, ReqId reqSeqNum) const;
  void handleClientPreProcessRequest(ClientPreProcessReqMsgSharedPtr clientReqMsg);
  void sendMsg(char *msg, NodeIdType dest, uint16_t msgType, MsgSize msgSize);
  void sendPreProcessRequestToAllReplicas(ClientPreProcessReqMsgSharedPtr clientPreProcessReqMsg, uint8_t sendReply);
  void registerClientPreProcessRequest(uint16_t clientId, ReqId requestSeqNum, ClientPreProcessReqMsgSharedPtr msg);
  void releaseClientPreProcessRequest(uint16_t clientId, ReqId requestSeqNum);
  uint16_t getClientReplyBufferId(uint16_t clientId) const { return clientId - numOfReplicas_; }
  uint32_t launchRequestPreProcessing(
      uint16_t clientId, ReqId reqSeqNum, uint32_t reqLength, char *reqBuf, char *resultBuf);
  void sendClientReplyMsg(ReplicaId senderId, NodeIdType clientId, SeqNum reqSeqNum);
  void asyncPreProcessing(ClientPreProcessReqMsgSharedPtr preProcessReqMsg);

 private:
  static std::vector<std::unique_ptr<PreProcessor>> preProcessors_;  // The place holder for PreProcessor objects

  std::shared_ptr<MsgsCommunicator> msgsCommunicator_;
  std::shared_ptr<IncomingMsgsStorage> incomingMsgsStorage_;
  std::shared_ptr<MsgHandlersRegistrator> msgHandlersRegistrator_;
  bftEngine::IRequestsHandler &requestsHandler_;
  const InternalReplicaApi &replica_;
  const ReplicaId replicaId_;
  const uint32_t maxReplyMsgSize_;
  const std::set<ReplicaId> &idsOfPeerReplicas_;
  const uint16_t numOfReplicas_;
  const uint16_t numOfClients_;
  util::SimpleThreadPool threadPool_;
  // One-time allocated buffers (one per client) for the pre-execution results storage
  std::vector<concordUtils::Sliver> preProcessResultBuffers_;
  // clientId -> *RequestProcessingInfo
  std::unordered_map<uint16_t, std::unique_ptr<RequestProcessingInfo>> ongoingRequests_;
  std::recursive_mutex ongoingRequestsMutex_;
};

//**************** Class AsyncPreProcessJob ****************//

// This class is used to send messages to other replicas in parallel

class AsyncPreProcessJob : public util::SimpleThreadPool::Job {
 public:
  AsyncPreProcessJob(PreProcessor &preProcessor, ClientPreProcessReqMsgSharedPtr msg, char *replyBuffer);
  virtual ~AsyncPreProcessJob() = default;

  void execute() override;
  void release() override;

 private:
  PreProcessor &preProcessor_;
  ClientPreProcessReqMsgSharedPtr msg_;
  char *replyBuffer_ = nullptr;
};

class AsyncSendPreProcessJob : public util::SimpleThreadPool::Job {
 public:
  AsyncSendPreProcessJob(PreProcessor &preProcessor, PreProcessRequestMsgSharedPtr msg, ReplicaId replicaId);
  virtual ~AsyncSendPreProcessJob() = default;

  void execute() override;
  void release() override;

 private:
  PreProcessor &preProcessor_;
  PreProcessRequestMsgSharedPtr msg_;
  ReplicaId destId_ = 0;
};

}  // namespace preprocessor
