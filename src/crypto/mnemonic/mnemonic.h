#ifndef MNEMONIC_H
#define MNEMONIC_H

#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <numeric>
#include "crypto/pkcs5_pbkdf2.h"

using WordList = std::vector<std::string>;
namespace mnemonic
{
    static constexpr size_t SEED_LENGTH = 64;
    static constexpr size_t MNEMONIC_WORD_COUNT = 12;
    std::array<uint8_t, SEED_LENGTH> mnemonicToSeed(const WordList& mnemonic, const std::string& passphrase = "");
    std::string unwords(const WordList& phrase);
}

#endif