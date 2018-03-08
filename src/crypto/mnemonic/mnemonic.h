#ifndef MNEMONIC_H
#define MNEMONIC_H

#include <algorithm>
#include <vector>
#include <string>
#include <numeric>
#include "crypto/pkcs5_pbkdf2.h"

using wordList = std::vector<std::string>;
namespace mnemonic
{
    uint8_t *mnemonicToSeed(const wordList& mnemonic, const std::string& passphrase = "");
}

#endif