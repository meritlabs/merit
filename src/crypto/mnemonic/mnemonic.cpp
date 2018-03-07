#include "crypto/mnemonic/mnemonic.h"
#include <iostream>
#include <algorithm>
#include <boost/format.hpp>

namespace mnemonic
{
    uint8_t *mnemonicToSeed(const wordList& mnemonic, const std::string& passphrase)
    {
        std::string mnemonicString = std::accumulate(std::next(mnemonic.begin()), mnemonic.end(), mnemonic[0],
            [](std::string left, std::string right)
            {
                return left + " " + right;
            });

        std::string saltString = "mnemonic" + passphrase;

        uint8_t *seed = new uint8_t[64];
        const uint8_t *password = reinterpret_cast<const uint8_t*>(mnemonicString.c_str());
        const uint8_t *salt = reinterpret_cast<const uint8_t*>(saltString.c_str());

        pkcs5_pbkdf2(password, mnemonicString.length(), salt, saltString.length(), seed, 64, 2048);

        return seed;
    }

    void print_uint8(const uint8_t* bytes, const int& num)
    {
        std::for_each(bytes, bytes + num,
            [](uint8_t c)
            {
                std::cerr << boost::format("%02x") % (int)c;
            });
        std::cerr << std::endl;
    }
}