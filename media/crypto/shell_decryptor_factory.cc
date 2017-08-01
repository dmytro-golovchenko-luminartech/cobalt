/*
 * Copyright 2012 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "media/crypto/shell_decryptor_factory.h"

namespace media {

// static
ShellDecryptorFactory::DecryptorRegistry ShellDecryptorFactory::registry_;

// static
bool ShellDecryptorFactory::Supports(const std::string& key_system) {
  DecryptorRegistry::iterator it = registry_.find(key_system);
  return it != registry_.end();
}

// static
Decryptor* ShellDecryptorFactory::Create(const std::string& key_system,
                                         DecryptorClient* client) {
  DecryptorRegistry::iterator it = registry_.find(key_system);
  if (it == registry_.end()) {
    return NULL;
  }
  return it->second.Run(client);
}

// static
void ShellDecryptorFactory::RegisterDecryptor(const std::string& key_system,
                                              const CreateCB& create_cb) {
  registry_[key_system] = create_cb;
}

}  // namespace media
