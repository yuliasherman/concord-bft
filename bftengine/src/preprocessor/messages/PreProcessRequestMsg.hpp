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

#include "messages/MessageBase.hpp"
#include "ClientPreProcessRequestMsg.hpp"

namespace preprocessor {

#pragma pack(push, 1)
struct PreProcessRequestMsgHeader {
  MessageBase::Header header;
  uint8_t sendReply;  // TBD: REMOVE
  ViewNum viewNum;
  SeqNum reqSeqNum;
  uint16_t clientId;
  NodeIdType senderId;
  uint32_t requestLength;
};
#pragma pack(pop)

class PreProcessRequestMsg : public MessageBase {
 public:
  PreProcessRequestMsg(NodeIdType senderId,
                       uint16_t clientId,
                       uint8_t sendReply,
                       uint64_t reqSeqNum,
                       ViewNum view,
                       uint32_t reqLength,
                       const char* request);

  char* requestBuf() const { return body() + sizeof(PreProcessRequestMsgHeader); }

  const uint32_t requestLength() const { return msgBody()->requestLength; }
  const uint16_t clientId() const { return msgBody()->clientId; }
  const SeqNum reqSeqNum() const { return msgBody()->reqSeqNum; }
  const ViewNum viewNum() const { return msgBody()->viewNum; }
  const uint8_t sendReply() const { return msgBody()->sendReply; }

  void setParams(
      NodeIdType senderId, uint16_t clientId, uint8_t sendReply, ReqId reqSeqNum, ViewNum view, uint32_t reqLength);

  static bool ToActualMsgType(MessageBase* inMsg, PreProcessRequestMsg*& outMsg);

 private:
  PreProcessRequestMsgHeader* msgBody() const { return ((PreProcessRequestMsgHeader*)msgBody_); }
};

typedef std::shared_ptr<PreProcessRequestMsg> PreProcessRequestMsgSharedPtr;

}  // namespace preprocessor
