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

#include "PrimitiveTypes.hpp"
#include "sha3_256.h"
#include "messages/PreProcessReplyMsg.hpp"
#include "messages/ClientPreProcessRequestMsg.hpp"
#include "sliver.hpp"

#include <vector>
#include <memory>

namespace preprocessor {

// This class collects and stores information relevant to the processing of one specific client request
// (for all replicas).

class RequestProcessingInfo {
 public:
  RequestProcessingInfo(uint16_t numOfReplicas, uint16_t numOfRequiredReplies, ReqId reqSeqNum);
  ~RequestProcessingInfo() = default;

  void saveClientPreProcessRequestMsg(ClientPreProcessReqMsgSharedPtr clientPreProcessRequestMsg);
  void savePreProcessReplyMsg(ReplicaId replicaId, PreProcessReplyMsgSharedPtr preProcessReplyMsg);
  void savePrimaryPreProcessResult(const concordUtils::Sliver &preProcessResult, uint32_t preProcessResultLen);
  ClientPreProcessReqMsgSharedPtr getClientPreProcessRequestMsg() const { return clientPreProcessRequestMsg_; }
  const SeqNum getReqSeqNum() const { return reqSeqNum_; }
  bool enoughRepliesReceived() const;

 private:
  const uint16_t numOfReplicas_;
  const uint16_t numOfRequiredReplies_;
  const ReqId reqSeqNum_;
  ClientPreProcessReqMsgSharedPtr clientPreProcessRequestMsg_;
  uint16_t numOfReceivedReplies_ = 0;
  concord::util::SHA3_256::Digest myPreProcessResultHash_ = {0};
  concordUtils::Sliver myPreProcessResult_;
  std::vector<PreProcessReplyMsgSharedPtr> replicasDataForRequest_;
};

}  // namespace preprocessor
