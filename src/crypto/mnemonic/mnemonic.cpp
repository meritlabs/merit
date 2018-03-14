#include "crypto/mnemonic/mnemonic.h"
#include <iostream>
#include <algorithm>
#include <boost/format.hpp>

namespace mnemonic
{
    std::array<uint8_t, SEED_LENGTH> mnemonicToSeed(const WordList& mnemonic, const std::string& passphrase)
    {
        return mnemonicToSeed(unwords(mnemonic), passphrase);
    }

    std::array<uint8_t, SEED_LENGTH> mnemonicToSeed(const std::string& mnemonic, const std::string& passphrase)
    {
        std::string salt = "mnemonic" + passphrase;

        std::array<uint8_t, SEED_LENGTH> seed;

        pkcs5_pbkdf2(mnemonic, salt, seed.begin(), SEED_LENGTH, 2048);

        return seed;
    }

    std::string unwords(const WordList& phrase)
    {
        if(phrase.size() > 0 )
            return std::accumulate(std::next(phrase.begin()), phrase.end(), phrase[0],
                [](std::string left, std::string right)
                {
                    return left + " " + right;
                });
        return "";
    }
}