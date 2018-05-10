#ifndef MNEMONIC_H
#define MNEMONIC_H

#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <numeric>
#include "crypto/pkcs5_pbkdf2.h"
#include "dictionary.h"

using WordList = std::vector<std::string>;
namespace mnemonic
{
    static constexpr size_t SEED_LENGTH = 64;
    static constexpr size_t MNEMONIC_WORD_COUNT = 12;
    static constexpr size_t ENTROPY_BYTES = 16;
    std::array<uint8_t, SEED_LENGTH> MnemonicToSeed(const WordList& mnemonic, const std::string& passphrase = "");
    std::array<uint8_t, SEED_LENGTH> MnemonicToSeed(const std::string& mnemonic, const std::string& passphrase = "");
    bool IsAValidMnemonic(const WordList& words);
    WordList MnemonicStringToWords(const std::string& mnemonic);
    std::string Unwords(const WordList& phrase);
    WordList Entropy2Mnemonic(const std::vector<uint8_t>& entropy, const language::Dictionary& dict);
}

#endif
