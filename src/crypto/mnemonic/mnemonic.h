#ifndef MNEMONIC_H
#define MNEMONIC_H

#include <algorithm>
#include <vector>
#include <string>
#include <numeric>
#include "crypto/pkcs5_pbkdf2.h"

using word_list = std::vector<std::string>;
namespace mnemonic
{
    uint8_t *mnemonic_to_seed(const word_list& mnemonic, const std::string& passphrase = "");
    void print_uint8(const uint8_t* bytes, const int& num = 0);
}

#endif