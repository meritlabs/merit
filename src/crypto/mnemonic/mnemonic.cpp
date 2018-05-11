#include "crypto/mnemonic/mnemonic.h"
#include "crypto/sha256.h"
#include <algorithm>
#include <bitset>
#include <sstream>
#include <string>
#include <boost/format.hpp>

namespace mnemonic
{
    namespace
    {
        std::string BinaryString(const uint8_t* data, const int& len)
        {
            std::stringstream ss;
            for(int i = 0; i < len; i++)
            {
                ss << std::bitset<8>((int)data[i]).to_string();
            }
            return ss.str();
        }
    }

    WordList MnemonicStringToWords(const std::string& mnemonic)
    {
        std::stringstream s;
        s << mnemonic;

        WordList words;
        while(s.good()) {
            std::string word;
            s >> word;

            if(!word.empty()) { 
                words.push_back(word);
            }
        }
        return words;
    }

    bool IsAValidMnemonic(const WordList& words) {
        return words.size() == MNEMONIC_WORD_COUNT;
    }

    std::array<uint8_t, SEED_LENGTH> MnemonicToSeed(const WordList& mnemonic, const std::string& passphrase)
    {
        return MnemonicToSeed(Unwords(mnemonic), passphrase);
    }

    std::array<uint8_t, SEED_LENGTH> MnemonicToSeed(const std::string& mnemonic, const std::string& passphrase)
    {
        std::string salt = "mnemonic" + passphrase;

        std::array<uint8_t, SEED_LENGTH> seed;

        pkcs5_pbkdf2(mnemonic, salt, seed.begin(), SEED_LENGTH, 2048);

        return seed;
    }

    std::string Unwords(const WordList& phrase)
    {
        if(phrase.size() > 0 )
            return std::accumulate(std::next(phrase.begin()), phrase.end(), phrase[0],
                [](std::string left, std::string right)
                {
                    return left + " " + right;
                });
        return "";
    }

    WordList Entropy2Mnemonic(const std::vector<uint8_t>& entropy, const language::Dictionary& dict)
    {
        std::vector<uint8_t> hash(CSHA256::OUTPUT_SIZE);

        CSHA256 sha256;
        sha256.Write(entropy.data(), entropy.size()).Finalize(hash.data());

        auto checksumBits = entropy.size() * 8 / 32;
        std::string checksumString = BinaryString(hash.data(), (checksumBits/8)+1).substr(0, checksumBits);
        std::string entString = BinaryString(entropy.data(), entropy.size()) + checksumString;

        assert(entString.size() % 11 == 0); // getting 11 bits to pick index in 0-2047
        assert(entString.size() / 11 == MNEMONIC_WORD_COUNT); // maybe this doesn't have to be constant?

        std::vector<int> inds(MNEMONIC_WORD_COUNT);
        for(size_t i = 0; i < entString.size() / 11; i++) {
            inds[i] = std::stoi(entString.substr(11*i, 11), nullptr, 2);
        }

        WordList mnemonic(MNEMONIC_WORD_COUNT);
        std::transform(inds.begin(), inds.end(), mnemonic.begin(), [&dict](const int& i) { return dict[i]; });

        assert(IsAValidMnemonic(mnemonic));
        return mnemonic;
    }
}
