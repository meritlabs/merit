#include "crypto/mnemonic/mnemonic.h"
#include <iostream>
#include <algorithm>
#include <boost/format.hpp>

namespace mnemonic
{
    uint8_t *mnemonic_to_key(const word_list& mnemonic, const std::string& passphrase)
    {
        std::string mnemonic_string = std::accumulate(std::next(mnemonic.begin()), mnemonic.end(), mnemonic[0],
            [](std::string left, std::string right)
            {
                return left + " " + right;
            });

        std::string salt_string = "mnemonic" + passphrase;

        uint8_t *key = new uint8_t[64];
        const uint8_t *password = reinterpret_cast<const uint8_t*>(mnemonic_string.c_str());
        const uint8_t *salt = reinterpret_cast<const uint8_t*>(salt_string.c_str());

        pkcs5_pbkdf2(password, mnemonic_string.length(), salt, salt_string.length(), key, 64, 2048);

        return key;
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