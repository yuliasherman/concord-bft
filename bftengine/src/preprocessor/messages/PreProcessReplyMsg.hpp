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
#include <memory>

namespace preprocessor {

#pragma pack(push, 1)
struct PreProcessReplyMsgHeader {
  MessageBase::Header header;
  ViewNum viewNum;
  SeqNum reqSeqNum;
  NodeIdType senderId;
  uint16_t clientId;
  uint32_t replyLength;
};
// The hash of pre-execution result is a part of the message body
#pragma pack(pop)

class PreProcessReplyMsg : public MessageBase {
 public:
  PreProcessReplyMsg(NodeIdType senderId,
                     uint16_t clientId,
                     uint64_t reqSeqNum,
                     ViewNum viewNum,
                     uint32_t replyLength,
                     const char* reply);

  void setParams(NodeIdType senderId, uint16_t clientId, ReqId reqSeqNum, ViewNum view, uint32_t replyLength);

  static bool ToActualMsgType(MessageBase* inMsg, PreProcessReplyMsg*& outMsg);

  const uint16_t clientId() const { return msgBody()->clientId; }
  const SeqNum reqSeqNum() const { return msgBody()->reqSeqNum; }
  const uint32_t replyLength() const { return msgBody()->replyLength; }

 private:
  PreProcessReplyMsgHeader* msgBody() const { return ((PreProcessReplyMsgHeader*)msgBody_); }
};

typedef std::shared_ptr<PreProcessReplyMsg> PreProcessReplyMsgSharedPtr;
}  // namespace preprocessor
