//Concord
//
//Copyright (c) 2019 VMware, Inc. All Rights Reserved.
//
//This product is licensed to you under the Apache 2.0 license (the "License").
// You may not use this product except in compliance with the Apache 2.0 License.
//
//This product may include a number of subcomponents with separate copyright
// notices and license terms. Your use of these subcomponents is subject to the
// terms and conditions of the subcomponent's license, as noted in the LICENSE
// file.

#include "ReplicaConfigSerializer.hpp"

using namespace std;

namespace bftEngine {

const string ReplicaConfigSerializer::className_ = "ReplicaConfig";
const uint32_t ReplicaConfigSerializer::classVersion_ = 1;
bool ReplicaConfigSerializer::registered_ = false;

void ReplicaConfigSerializer::registerClass() {
  if (!registered_) {
    classNameToObjectMap_[className_] =
        UniquePtrToClass(new ReplicaConfigSerializer);
    registered_ = true;
  }
}

/************** Serialization **************/

void ReplicaConfigSerializer::serialize(UniquePtrToChar &outBuf,
                                        int64_t &outBufSize) const {
  ofstream outStream(className_.c_str(), ofstream::binary | ofstream::trunc);
  // Serialize first the class name.
  serializeClassName(className_, outStream);
  serializeDataMembers(outStream);
  outStream.close();
  retrieveSerializedBuffer(className_, outBuf, outBufSize);
}

void ReplicaConfigSerializer::serializeDataMembers(ostream &outStream) const {
  // Serialize class version
  outStream.write((char *) &classVersion_, sizeof(classVersion_));

  // Serialize fVal
  outStream.write((char *) &config_->fVal, sizeof(config_->fVal));

  // Serialize cVal
  outStream.write((char *) &config_->cVal, sizeof(config_->cVal));

  // Serialize replicaId
  outStream.write((char *) &config_->replicaId, sizeof(config_->replicaId));

  // Serialize numOfClientProxies
  outStream.write((char *) &config_->numOfClientProxies,
                  sizeof(config_->numOfClientProxies));

  // Serialize statusReportTimerMillisec
  outStream.write((char *) &config_->statusReportTimerMillisec,
                  sizeof(config_->statusReportTimerMillisec));

  // Serialize concurrencyLevel
  outStream.write((char *) &config_->concurrencyLevel,
                  sizeof(config_->concurrencyLevel));

  // Serialize autoViewChangeEnabled
  outStream.write((char *) &config_->autoViewChangeEnabled,
                  sizeof(config_->autoViewChangeEnabled));

  // Serialize viewChangeTimerMillisec
  outStream.write((char *) &config_->viewChangeTimerMillisec,
                  sizeof(config_->viewChangeTimerMillisec));

  // Serialize public keys
  auto numOfPublicKeys = (int64_t) config_->publicKeysOfReplicas.size();
  outStream.write((char *) &numOfPublicKeys, sizeof(numOfPublicKeys));
  for (auto elem : config_->publicKeysOfReplicas) {
    outStream.write((char *) &elem.first, sizeof(elem.first));
    serializeKey(elem.second, outStream);
  }

  // Serialize replicaPrivateKey
  serializeKey(config_->replicaPrivateKey, outStream);

  config_->thresholdSignerForExecution->serialize(outStream);
  config_->thresholdVerifierForExecution->serialize(outStream);

  config_->thresholdSignerForSlowPathCommit->serialize(outStream);
  config_->thresholdVerifierForSlowPathCommit->serialize(outStream);

  config_->thresholdSignerForCommit->serialize(outStream);
  config_->thresholdVerifierForCommit->serialize(outStream);

  config_->thresholdSignerForOptimisticCommit->serialize(outStream);
  config_->thresholdVerifierForOptimisticCommit->serialize(outStream);
}

void ReplicaConfigSerializer::serializeKey(
    const string &key, ostream &outStream) const {
  auto sizeOfKey = (int64_t) key.size();
  // Save a length of the string to the buffer to be able to deserialize it.
  outStream.write((char *) &sizeOfKey, sizeof(sizeOfKey));
  outStream.write(key.c_str(), sizeOfKey);
}

bool ReplicaConfigSerializer::operator==(const ReplicaConfigSerializer &other)
const {
  bool result = ((other.config_->fVal == config_->fVal) &&
      (other.config_->cVal == config_->cVal) &&
      (other.config_->replicaId == config_->replicaId) &&
      (other.config_->numOfClientProxies == config_->numOfClientProxies) &&
      (other.config_->statusReportTimerMillisec ==
          config_->statusReportTimerMillisec) &&
      (other.config_->concurrencyLevel == config_->concurrencyLevel) &&
      (other.config_->autoViewChangeEnabled ==
          config_->autoViewChangeEnabled) &&
      (other.config_->viewChangeTimerMillisec ==
          config_->viewChangeTimerMillisec) &&
      (other.config_->publicKeysOfReplicas == config_->publicKeysOfReplicas) &&
      (other.config_->replicaPrivateKey == config_->replicaPrivateKey));
  return result;
}

/************** Deserialization **************/

UniquePtrToClass ReplicaConfigSerializer::create(istream &inStream) {
  verifyClassName(className_, inStream);

  // Deserialize class version
  verifyClassVersion(classVersion_, inStream);

  // Deserialize fVal
  inStream.read((char *) &config_->fVal, sizeof(config_->fVal));

  // Deserialize cVal
  inStream.read((char *) &config_->cVal, sizeof(config_->cVal));

  // Deserialize replicaId
  inStream.read((char *) &config_->replicaId, sizeof(config_->replicaId));

  // Deserialize numOfClientProxies
  inStream.read((char *) &config_->numOfClientProxies,
                sizeof(config_->numOfClientProxies));

  // Deserialize statusReportTimerMillisec
  inStream.read((char *) &config_->statusReportTimerMillisec,
                sizeof(config_->statusReportTimerMillisec));

  // Deserialize concurrencyLevel
  inStream.read((char *) &config_->concurrencyLevel,
                sizeof(config_->concurrencyLevel));

  // Deserialize autoViewChangeEnabled
  inStream.read((char *) &config_->autoViewChangeEnabled,
                sizeof(config_->autoViewChangeEnabled));

  // Deserialize viewChangeTimerMillisec
  inStream.read((char *) &config_->viewChangeTimerMillisec,
                sizeof(config_->viewChangeTimerMillisec));

  // Deserialize public keys
  int64_t numOfPublicKeys = 0;
  inStream.read((char *) &numOfPublicKeys, sizeof(numOfPublicKeys));
  for (int i = 0; i < numOfPublicKeys; ++i) {
    uint16_t id = 0;
    inStream.read((char *) &id, sizeof(id));
    string key = deserializeKey(inStream);
    config_->publicKeysOfReplicas.insert(pair<uint16_t, string>(id, key));
  }

  // Serialize replicaPrivateKey
  config_->replicaPrivateKey = deserializeKey(inStream);

  createSignersAndVerifiers(inStream);
  return UniquePtrToClass(this);
}

void ReplicaConfigSerializer::createSignersAndVerifiers(istream &inStream) {
  delete config_->thresholdSignerForExecution;
  config_->thresholdSignerForExecution =
      dynamic_cast<IThresholdSigner *>(deserialize(inStream).get());

  delete config_->thresholdVerifierForExecution;
  config_->thresholdVerifierForExecution =
      dynamic_cast<IThresholdVerifier *>(deserialize(inStream).get());

  delete config_->thresholdSignerForSlowPathCommit;
  config_->thresholdSignerForSlowPathCommit =
      dynamic_cast<IThresholdSigner *>(deserialize(inStream).get());

  delete config_->thresholdVerifierForSlowPathCommit;
  config_->thresholdVerifierForSlowPathCommit =
      dynamic_cast<IThresholdVerifier *>(deserialize(inStream).get());

  delete config_->thresholdSignerForCommit;
  config_->thresholdSignerForCommit =
      dynamic_cast<IThresholdSigner *>(deserialize(inStream).get());

  delete config_->thresholdVerifierForCommit;
  config_->thresholdVerifierForCommit =
      dynamic_cast<IThresholdVerifier *>(deserialize(inStream).get());

  delete config_->thresholdSignerForOptimisticCommit;
  config_->thresholdSignerForOptimisticCommit =
      dynamic_cast<IThresholdSigner *>(deserialize(inStream).get());

  delete config_->thresholdVerifierForOptimisticCommit;
  config_->thresholdVerifierForOptimisticCommit =
      dynamic_cast<IThresholdVerifier *>(deserialize(inStream).get());
}

string ReplicaConfigSerializer::deserializeKey(istream &inStream) const {
  int64_t sizeOfKey = 0;
  inStream.read((char *) &sizeOfKey, sizeof(sizeOfKey));
  UniquePtrToChar key(new char[sizeOfKey]);
  inStream.read(key.get(), sizeOfKey);
  return key.get();
}

}
