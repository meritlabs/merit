// Copyright (c) 2017-2018 The Merit Foundation developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "refdb.h"

#include "base58.h"
#include <boost/rational.hpp>
#include <limits>

namespace pog
{
    extern bool IsValidAmbassadorDestination(char type);
}

namespace referral
{
    namespace
    {
        const char DB_CHILDREN = 'c';
        const char DB_REFERRALS = 'r';
        const char DB_HASH = 'h';
        const char DB_PARENT_ADDRESS = 'p';
        const char DB_ANV = 'a';
        const char DB_PUBKEY = 'k';
        const char DB_LOT_SIZE = 's';
        const char DB_LOT_VAL = 'v';
        const char DB_CONFIRMATION = 'i';
        const char DB_CONFIRMATION_IDX = 'n';
        const char DB_CONFIRMATION_TOTAL = 'u';
        const char DB_PRE_DAEDALUS_CONFIRMED = 'd';
        const char DB_ALIAS = 'l';

        const size_t MAX_LEVELS = std::numeric_limits<size_t>::max();
    }

    //stores ANV internally as a rational number with numerator/denominator
    using AnvInternal = std::pair<CAmount, CAmount>;
    using ANVTuple = std::tuple<char, Address, AnvInternal>;
    using AnvRat = boost::rational<CAmount>;
    using TransactionOutIndex = int;
    using ConfirmationVal = std::pair<char, Address>;

    namespace {
        class ReferralIdVisitor : public boost::static_visitor<MaybeReferral>
        {
            private:
                const ReferralsViewDB *db;
            public:
                explicit ReferralIdVisitor(const ReferralsViewDB *db_in): db{db_in} {}

                template <typename T>
                    MaybeReferral operator()(const T &id) const {
                        return db->GetReferral(id);
                    }
        };
    }

    ReferralsViewDB::ReferralsViewDB(
            size_t cache_size,
            bool memory,
            bool wipe,
            const std::string& db_name) : m_db(GetDataDir() / db_name, cache_size, memory, wipe, true) {}

    MaybeReferral ReferralsViewDB::GetReferral(const Address& address) const
    {
        MutableReferral referral;
        return m_db.Read(std::make_pair(DB_REFERRALS, address), referral) ?
            MaybeReferral{referral} :
            MaybeReferral{};
    }

    MaybeReferral ReferralsViewDB::GetReferral(const uint256& hash) const
    {
        Address address;
        if (m_db.Read(std::make_pair(DB_HASH, hash), address)) {
            return GetReferral(address);
        }

        return {};
    }

    MaybeReferral ReferralsViewDB::GetReferral(const std::string& alias) const
    {
        if (alias.size() == 0 || alias.size() > MAX_ALIAS_LENGTH) {
            return {};
        }

        Address address;
        if (m_db.Read(std::make_pair(DB_ALIAS, alias), address)) {
            return GetReferral(address);
        }

        return {};
    }

    MaybeReferral ReferralsViewDB::GetReferral(const ReferralId& referral_id) const
    {
        return boost::apply_visitor(ReferralIdVisitor(this), referral_id);
    }


    MaybeAddress ReferralsViewDB::GetAddressByPubKey(const CPubKey& pubkey) const
    {
        Address address;
        return m_db.Read(std::make_pair(DB_PUBKEY, pubkey), address) ? MaybeAddress{address} : MaybeAddress{};
    }

    MaybeAddressPair ReferralsViewDB::GetParentAddress(const Address& address) const
    {
        AddressPair parent;
        return m_db.Read(std::make_pair(DB_PARENT_ADDRESS, address), parent) ?
            MaybeAddressPair{parent} :
            MaybeAddressPair{};
    }

    ChildAddresses ReferralsViewDB::GetChildren(const Address& address) const
    {
        ChildAddresses children;
        m_db.Read(std::make_pair(DB_CHILDREN, address), children);
        return children;
    }

    bool ReferralsViewDB::InsertReferral(const Referral& referral, bool allow_no_parent)
    {
        debug("Inserting referral %s parent %s",
                CMeritAddress{referral.addressType, referral.GetAddress()}.ToString(),
                referral.parentAddress.GetHex());

        if(referral.alias.size() > MAX_ALIAS_LENGTH) {
            return false;
        }

        if (Exists(referral.GetAddress())) {
            return true;
        }

        //write referral by code hash
        if (!m_db.Write(std::make_pair(DB_REFERRALS, referral.GetAddress()), referral)) {
            return false;
        }

        ANVTuple anv{referral.addressType, referral.GetAddress(), AnvInternal{0, 1}};
        if (!m_db.Write(std::make_pair(DB_ANV, referral.GetAddress()), anv)) {
            return false;
        }

        // write referral address by hash
        if (!m_db.Write(std::make_pair(DB_HASH, referral.GetHash()), referral.GetAddress()))
            return false;

        // write referral address by pubkey
        if (!m_db.Write(std::make_pair(DB_PUBKEY, referral.pubkey), referral.GetAddress()))
            return false;

        if (referral.version >= Referral::INVITE_VERSION && referral.alias.size() > 0) {
            // write referral referral address by alias
            if (!m_db.Write(std::make_pair(DB_ALIAS, referral.alias), referral.GetAddress()))
                return false;
        }

        // Typically because the referral should be written in order we should
        // be able to find the parent referral. We can then write the child->parent
        // mapping of public addresses
        if (auto parent_referral = GetReferral(referral.parentAddress)) {
            debug("\tInserting parent reference %s parent %s",
                    CMeritAddress{referral.addressType, referral.GetAddress()}.ToString(),
                    CMeritAddress{parent_referral->addressType, parent_referral->GetAddress()}.ToString());

            const auto parent_address = parent_referral->GetAddress();
            AddressPair parent_addr_pair{parent_referral->addressType, parent_address};
            if (!m_db.Write(std::make_pair(DB_PARENT_ADDRESS, referral.GetAddress()), parent_addr_pair))
                return false;

            // Now we update the children of the parent address by inserting into the
            // child address array for the parent.
            ChildAddresses children;
            m_db.Read(std::make_pair(DB_CHILDREN, referral.parentAddress), children);

            children.push_back(referral.GetAddress());

            if (!m_db.Write(std::make_pair(DB_CHILDREN, referral.parentAddress), children))
                return false;

            debug("Inserted referral %s parent %s",
                    CMeritAddress{referral.addressType, referral.GetAddress()}.ToString(),
                    CMeritAddress{parent_referral->addressType, referral.parentAddress}.ToString());

        } else if (!allow_no_parent) {
            assert(false && "parent referral missing");
            return false;
        } else {
            debug("\tWarning Parent missing for address %s. Parent: %s",
                    CMeritAddress{referral.addressType, referral.GetAddress()}.ToString(),
                    referral.parentAddress.GetHex());
        }

        return true;
    }

    bool ReferralsViewDB::RemoveReferral(const Referral& referral)
    {
        debug("Removing Referral %d", CMeritAddress{referral.addressType, referral.GetAddress()}.ToString());

        if (!m_db.Erase(std::make_pair(DB_REFERRALS, referral.GetAddress())))
            return false;

        if (!m_db.Erase(std::make_pair(DB_HASH, referral.GetHash())))
            return false;

        if (!m_db.Erase(std::make_pair(DB_PUBKEY, referral.pubkey)))
            return false;

        if (!m_db.Erase(std::make_pair(DB_PARENT_ADDRESS, referral.GetAddress())))
            return false;

        ChildAddresses children;
        m_db.Read(std::make_pair(DB_CHILDREN, referral.parentAddress), children);

        children.erase(std::remove(std::begin(children), std::end(children),
                    referral.GetAddress()),
                std::end(children));

        if (!m_db.Write(std::make_pair(DB_CHILDREN, referral.parentAddress), children))
            return false;

        return true;
    }

    /**
     * Updates ANV for the address and all parents. Note change can be negative if
     * there was a debit.
     *
     * Internally ANV values are stored as rational numbers because as ANV values
     * bubble up, they get halved each step along the way and we need to make sure
     * to account for values at the sub-micro level. This design discourages
     * creating long chains of referrals and rewards those who grow wider trees.
     */

    bool ReferralsViewDB::UpdateANV(
            char address_type,
            const Address& start_address,
            CAmount change)
    {
        AnvRat change_rat = change;

        debug("\tUpdateANV: %s + %d",
                CMeritAddress(address_type, start_address).ToString(), change);

        MaybeAddress address = start_address;
        size_t level = 0;

        //MAX_LEVELS guards against cycles in DB
        while (address && change != 0 && level < MAX_LEVELS) {
            //it's possible address didn't exist yet so an ANV of 0 is assumed.
            ANVTuple anv;
            if (!m_db.Read(std::make_pair(DB_ANV, *address), anv)) {
                debug("\tFailed to read ANV for %s", address->GetHex());
                return false;
            }

            assert(std::get<0>(anv) != 0);
            assert(!std::get<1>(anv).IsNull());

            debug(
                    "\t\t %d %s %d/%d + %d",
                    level,
                    CMeritAddress(std::get<0>(anv), std::get<1>(anv)).ToString(),
                    std::get<0>((std::get<2>(anv))),
                    std::get<1>((std::get<2>(anv))),
                    change);

            auto& anv_in = std::get<2>(anv);

            AnvRat anv_rat{anv_in.first, anv_in.second};

            anv_rat += change_rat;

            anv_in.first = anv_rat.numerator();
            anv_in.second = anv_rat.denominator();

            if (!m_db.Write(std::make_pair(DB_ANV, *address), anv)) {
                //TODO: Do we rollback anv computation for already processed address?
                // likely if we can't write then rollback will fail too.
                // figure out how to mark database as corrupt.
                return false;
            }

            const auto parent = GetParentAddress(*address);
            if (parent) {
                address_type = parent->first;
                address = parent->second;
            } else {
                address.reset();
            }
            level++;
            change_rat /= 2;
        }

        // We should never have cycles in the DB.
        // Hacked? Bug?
        assert(level < MAX_LEVELS && "reached max levels. Referral DB cycle detected");
        return true;
    }

    CAmount AnvInToAnvPub(const AnvInternal& in)
    {
        AnvRat anv_rat{in.first, in.second};
        return boost::rational_cast<CAmount>(anv_rat);
    }

    MaybeAddressANV ReferralsViewDB::GetANV(const Address& address) const
    {
        ANVTuple anv;
        if (!m_db.Read(std::make_pair(DB_ANV, address), anv)) {
            return MaybeAddressANV{};
        }

        const auto anv_pub = AnvInToAnvPub(std::get<2>(anv));
        return MaybeAddressANV({
                std::get<0>(anv),
                std::get<1>(anv),
                anv_pub
                });
    }

    AddressANVs ReferralsViewDB::GetAllANVs() const
    {
        std::unique_ptr<CDBIterator> iter{m_db.NewIterator()};
        iter->SeekToFirst();

        AddressANVs anvs;
        auto address = std::make_pair(DB_ANV, Address{});
        while (iter->Valid()) {
            //filter non ANV addresss
            if (!iter->GetKey(address)) {
                iter->Next();
                continue;
            }

            if (address.first != DB_ANV) {
                iter->Next();
                continue;
            }

            ANVTuple anv;
            if (!iter->GetValue(anv)) {
                iter->Next();
                continue;
            }

            const auto anv_pub = AnvInToAnvPub(std::get<2>(anv));
            anvs.push_back({std::get<0>(anv),
                    std::get<1>(anv),
                    anv_pub});

            iter->Next();
        }
        return anvs;
    }

    void ReferralsViewDB::GetAllRewardableANVs(
            const Consensus::Params& params,
            int height,
            referral::AddressANVs& entrants) const
    {
        const auto heap_size = GetLotteryHeapSize();
        bool found_genesis = false;
        for (uint64_t i = 0; i < heap_size; i++) {
            LotteryEntrant v;
            if (!m_db.Read(std::make_pair(DB_LOT_VAL, i), v)) {
                break;
            }

            auto maybe_anv = GetANV(std::get<2>(v));
            if (!maybe_anv) {
                break;
            }

            if(maybe_anv->address_type != 1 && maybe_anv->address_type != 2) {
                continue;
            }

            /*
             * After block 13499 the genesis address does not participate in the lottery.
             * So don't include the genesis address as an entrant.
             */
            if(!found_genesis && height >= 13500 && maybe_anv->address == params.genesis_address) {
                found_genesis = true;
                continue;
            }

            entrants.push_back(*maybe_anv);
        }
    }

    bool ReferralsViewDB::FindLotteryPos(const Address& address, uint64_t& pos) const
    {
        const auto heap_size = GetLotteryHeapSize();
        for (uint64_t i = 0; i < heap_size; i++) {
            LotteryEntrant v;
            if (!m_db.Read(std::make_pair(DB_LOT_VAL, i), v)) {
                return false;
            }

            if (std::get<2>(v) == address) {
                pos = i;
                return true;
            }
        }

        pos = heap_size;
        return true;
    }

    /**
     * This function uses a modified version of the weighted random sampling algorithm
     * by Efraimidis and Spirakis
     * (https://www.sciencedirect.com/science/article/pii/S002001900500298X).
     *
     * Instead of computing R=rand^(1/W) where rand is some uniform random value
     * between [0,1] and W is the ANV, we will compute log(R).
     *
     * See pog::WeightedKeyForSampling for more details.
     *
     * A MinHeap is maintained in the DB and this function, once it decides if
     * it wants to add the address, it uses InsertLotteryEntrant to insert and
     * maintain the heap.
     */
    bool ReferralsViewDB::AddAddressToLottery(
            int height,
            uint256 rand_value,
            char address_type,
            MaybeAddress address,
            const uint64_t max_reservoir_size,
            LotteryUndos& undos)
    {
        auto maybe_anv = GetANV(*address);
        if (!maybe_anv) return false;

        //Don't include non key or script addresses.
        //parameterized addresses are not allowed.
        if(!pog::IsValidAmbassadorDestination(address_type)) {
            //return true here because false means bad error.
            return true;
        }

        size_t levels = 0;
        while (address && levels < MAX_LEVELS) {
            /**
             * Fix for the sampling algorithm before the reservoir is full.
             * The bug has little impact when the reservoir is not full.
             */
            if (height >= 16000) {
                maybe_anv = GetANV(*address);
                if (!maybe_anv) return false;

                //combine hashes and hash to get next sampling value
                CHashWriter hasher{SER_DISK, CLIENT_VERSION};
                hasher << rand_value << *address;
                rand_value = hasher.GetHash();
            }

            const auto weighted_key = pog::WeightedKeyForSampling(rand_value, maybe_anv->anv);
            const auto heap_size = GetLotteryHeapSize();

            debug("Lottery: Attempting to add %s with weighted Key %d",
                    CMeritAddress(address_type, *address).ToString(),
                    static_cast<double>(weighted_key));

            // Note we are duplicating FindLotterPos inside both if conditions because
            // once the reservoir is full, we won't be attempting to add every time
            // so it is silly to check for duplicates if we aren't going to add anyway.

            if (heap_size < max_reservoir_size) {
                uint64_t pos;
                if (!FindLotteryPos(*address, pos)) {
                    return false;
                }

                //Only add entrants that are not already participating.
                if (pos == heap_size) {
                    if (!InsertLotteryEntrant(
                                weighted_key,
                                address_type,
                                *address,
                                max_reservoir_size)) {
                        return false;
                    }

                    LotteryUndo undo{
                        weighted_key,
                        address_type,
                        *address,
                        *address};

                    undos.emplace_back(undo);
                } else {
                    debug("\tLottery: %s is already in the lottery.",
                            CMeritAddress(address_type, *address).ToString());
                }
            } else {
                const auto maybe_min_entrant = GetMinLotteryEntrant();
                if (!maybe_min_entrant) {
                    return false;
                }

                const auto min_weighted_key = std::get<0>(*maybe_min_entrant);
                //Insert into reservoir only if the new key is bigger
                //than the smallest key already there. Over time as the currency
                //grows in amount there should always be a key greater than the
                //smallest at some point as time goes on.
                if (min_weighted_key < weighted_key) {
                    uint64_t pos;
                    if (!FindLotteryPos(*address, pos)) {
                        return false;
                    }

                    //Only add entrants that are not already participating.
                    if (pos == heap_size) {
                        if (!PopMinFromLotteryHeap()) {
                            return false;
                        }

                        if (!InsertLotteryEntrant(
                                    weighted_key,
                                    address_type,
                                    *address,
                                    max_reservoir_size)) {
                            return false;
                        }

                        LotteryUndo undo{
                            std::get<0>(*maybe_min_entrant),
                            std::get<1>(*maybe_min_entrant),
                            std::get<2>(*maybe_min_entrant),
                            *address};

                        undos.emplace_back(undo);
                    } else {
                        debug("\tLottery: %s is already in the lottery.",
                                CMeritAddress(address_type, *address).ToString());
                    }
                } else {
                    debug("\tLottery: %s didn't make the cut with key %d, min %d",
                            CMeritAddress(address_type, *address).ToString(),
                            static_cast<double>(weighted_key),
                            static_cast<double>(min_weighted_key));
                }
            }

            const auto parent = GetParentAddress(*address);
            if (parent) {
                address_type = parent->first;
                address = parent->second;
            } else {
                address.reset();
            }
            levels++;
        }

        return true;
    }

    bool ReferralsViewDB::UndoLotteryEntrant(
            const LotteryUndo& undo,
            const uint64_t max_reservoir_size)
    {
        if (!RemoveFromLottery(undo.replaced_with)) {
            return false;
        }

        //Undo where the replaced address is the same as replaced_with are considered
        //add only and we just remove the entry and not try to add it.
        if (undo.replaced_with == undo.replaced_address) {
            return true;
        }

        if (!InsertLotteryEntrant(
                    undo.replaced_key,
                    undo.replaced_address_type,
                    undo.replaced_address,
                    max_reservoir_size)) {
            return false;
        }

        return true;
    }

    uint64_t ReferralsViewDB::GetLotteryHeapSize() const
    {
        uint64_t size = 0;
        m_db.Read(DB_LOT_SIZE, size);
        return size;
    }

    MaybeLotteryEntrant ReferralsViewDB::GetMinLotteryEntrant() const
    {
        LotteryEntrant v;

        const uint64_t first = 0;
        return m_db.Read(std::make_pair(DB_LOT_VAL, first), v) ?
            MaybeLotteryEntrant{v} :
            MaybeLotteryEntrant{};
    }

    /**
     * The addresses in a lottery are kept in a MinHeap. This function inserts
     * the address at the end and bubbles it up to the correct spot swapping with
     * parents until the right spot is found. If this function returns false, then
     * some bad things happened. You must not call this function when the heap is
     * full. You first must pop an element off the heap using PopMinFromLotteryHeap
     */
    bool ReferralsViewDB::InsertLotteryEntrant(
            const pog::WeightedKey& key,
            char address_type,
            const Address& address,
            const uint64_t max_reservoir_size)
    {
        auto heap_size = GetLotteryHeapSize();
        assert(heap_size < max_reservoir_size);

        auto pos = heap_size;

        while (pos != 0) {
            const auto parent_pos = (pos - 1) / 2;

            LotteryEntrant parent_value;
            if (!m_db.Read(std::make_pair(DB_LOT_VAL, parent_pos), parent_value)) {
                return false;
            }

            //We found our spot
            if (key > std::get<0>(parent_value)) {
                break;
            }

            //Push our parent down since we are moving up.
            if (!m_db.Write(std::make_tuple(DB_LOT_VAL, pos), parent_value)) {
                return false;
            }

            pos = parent_pos;
        }

        //write final value
        debug("\tAdding to Reservoir %s at pos %d", CMeritAddress(address_type, address).ToString(), pos);
        if (!m_db.Write(std::make_pair(DB_LOT_VAL, pos), std::make_tuple(key, address_type, address))) {
            return false;
        }

        uint64_t new_size = heap_size + 1;
        if (!m_db.Write(DB_LOT_SIZE, new_size))
            return false;

        assert(new_size <= max_reservoir_size);
        return true;
    }

    bool ReferralsViewDB::PopMinFromLotteryHeap()
    {
        return RemoveFromLottery(0);
    }

    bool ReferralsViewDB::RemoveFromLottery(const Address& to_remove)
    {
        uint64_t pos;
        if (!FindLotteryPos(to_remove, pos)) {
            return false;
        }
        return RemoveFromLottery(pos);
    }

    bool ReferralsViewDB::RemoveFromLottery(uint64_t current)
    {
        debug("\tPopping from lottery reservoir position %d", current);
        auto heap_size = GetLotteryHeapSize();
        if (heap_size == 0) return false;

        LotteryEntrant last;
        if (!m_db.Read(std::make_pair(DB_LOT_VAL, heap_size - 1), last)) {
            return false;
        }

        LotteryEntrant smallest_val = last;

        //Walk down heap and bubble down the last value until we find the correct spot.
        while (true) {
            uint64_t smallest = current;
            uint64_t left = 2 * current + 1;
            uint64_t right = 2 * current + 2;

            if (left < heap_size) {
                LotteryEntrant left_val;
                if (!m_db.Read(std::make_pair(DB_LOT_VAL, left), left_val)) {
                    return false;
                }

                if (std::get<0>(left_val) < std::get<0>(smallest_val)) {
                    smallest = left;
                    smallest_val = left_val;
                }
            }

            if (right < heap_size) {
                LotteryEntrant right_val;
                if (!m_db.Read(std::make_pair(DB_LOT_VAL, right), right_val)) {
                    return false;
                }

                if (std::get<0>(right_val) < std::get<0>(smallest_val)) {
                    smallest = right;
                    smallest_val = right_val;
                }
            }

            if (smallest != current) {
                //write the current element with the smallest
                if (!m_db.Write(std::make_pair(DB_LOT_VAL, current), smallest_val)) {
                    return false;
                }

                //now go down the smallest path
                current = smallest;
            } else {
                break;
            }
        }

        //finally write the value in the correct spot and reduce the heap
        //size by 1
        if (!m_db.Write(std::make_pair(DB_LOT_VAL, current), last)) {
            return false;
        }

        uint64_t new_size = heap_size - 1;
        if (!m_db.Write(DB_LOT_SIZE, new_size)) {
            return false;
        }

        debug("\tPopped from lottery reservoir, last ended up at %d", current);
        return true;
    }

    /*
     * Orders referrals by constructing a dependency graph and doing a breath
     * first walk through the forrest.
     */
    bool ReferralsViewDB::OrderReferrals(referral::ReferralRefs& refs)
    {
        if (refs.empty()) {
            return true;
        }

        auto end_roots =
            std::partition(refs.begin(), refs.end(),
                    [this](const referral::ReferralRef& ref) -> bool {
                    return static_cast<bool>(GetReferral(ref->parentAddress));
                    });

        //If we don't have any roots, we have an invalid block.
        if (end_roots == refs.begin()) {
            return false;
        }

        std::map<uint160, referral::ReferralRefs> graph;

        //insert roots of trees into graph
        std::for_each(
                refs.begin(), end_roots,
                [&graph](const referral::ReferralRef& ref) {
                graph[ref->GetAddress()] = referral::ReferralRefs{};
                });

        //Insert disconnected referrals
        std::for_each(end_roots, refs.end(),
                [&graph](const referral::ReferralRef& ref) {
                graph[ref->parentAddress].push_back(ref);
                });

        //copy roots to work queue
        std::deque<referral::ReferralRef> to_process(std::distance(refs.begin(), end_roots));
        std::copy(refs.begin(), end_roots, to_process.begin());

        //do breath first walk through the trees to create correct referral
        //ordering
        auto replace = refs.begin();
        while (!to_process.empty() && replace != refs.end()) {
            const auto& ref = to_process.front();
            *replace = ref;
            to_process.pop_front();
            replace++;

            const auto& children = graph[ref->GetAddress()];
            to_process.insert(to_process.end(), children.begin(), children.end());
        }

        //If any of these conditions are not met, it means we have an invalid block
        if (replace != refs.end() || !to_process.empty()) {
            return false;
        }

        return true;
    }

    //Confirmation pair is an index plus count
    using ConfirmationPair = std::pair<uint64_t, int>;

    bool ReferralsViewDB::UpdateConfirmation(
            char address_type,
            const Address& address,
            CAmount amount)
    {
        uint64_t total_confirmations = 0;
        m_db.Read(DB_CONFIRMATION_TOTAL, total_confirmations);

        bool new_confirmation = false;
        bool delete_confirmation = false;
        ConfirmationPair confirmation;
        if(!m_db.Read(
                    std::make_pair(DB_CONFIRMATION, address),
                    confirmation)) {
            confirmation.first = total_confirmations;
            confirmation.second = amount;
            new_confirmation = true;
        } else {
            confirmation.second += amount;

            //We delete the last confirmation only if amount of invites reaches
            //0 and it is the last confirmation in the array. This is to handle
            //DisconnectBlock correctly.
            assert(total_confirmations > 0);

            delete_confirmation = confirmation.second == 0
                && confirmation.first == total_confirmations - 1;

            if(confirmation.second < 0) {
                return false;
            }
        }

        if (!m_db.Write(
                    std::make_pair(DB_CONFIRMATION, address),
                    confirmation)) {
            return false;
        }

        if(new_confirmation) {
            if (!m_db.Write(
                        std::make_pair(DB_CONFIRMATION_IDX, total_confirmations),
                        std::make_pair(
                            address_type,
                            address))) {
                return false;
            }

            if(!m_db.Write(DB_CONFIRMATION_TOTAL, total_confirmations + 1)) {
                return false;
            }
        } else if (delete_confirmation) {
            if(!m_db.Write(DB_CONFIRMATION_TOTAL, total_confirmations - 1)) {
                return false;
            }
            if(!m_db.Erase(std::make_pair(DB_CONFIRMATION, address))) {
                return false;
            }
            if(!m_db.Erase(std::make_pair(DB_CONFIRMATION_IDX, confirmation.first))) {
                return false;
            }
        }
        return true;
    }

    bool ReferralsViewDB::Exists(const referral::Address& address) const
    {
        return m_db.Exists(std::make_pair(DB_REFERRALS, address));
    }

    bool ReferralsViewDB::Exists(const std::string& alias) const
    {
        return alias.size() > 0 && m_db.Exists(std::make_pair(DB_ALIAS, alias));
    }

    bool ReferralsViewDB::IsConfirmed(const referral::Address& address) const
    {
        ConfirmationPair confirmation;
        if(!m_db.Read(
                    std::make_pair(DB_CONFIRMATION, address),
                    confirmation)) {
            return false;
        }
        return confirmation.second > 0;
    }

    using AddressPairs = std::vector<AddressPair>;

    bool ReferralsViewDB::ConfirmAllPreDaedalusAddresses()
    {
        //Check to see if addresses have already been confirmed.
        if (m_db.Exists(DB_PRE_DAEDALUS_CONFIRMED)) {
            return true;
        }

        std::unique_ptr<CDBIterator> iter{m_db.NewIterator()};
        iter->SeekToFirst();

        AddressPairs addresses;

        auto address = std::make_pair(DB_REFERRALS, Address{});
        while (iter->Valid()) {
            // filter non ANV addresses
            if (!iter->GetKey(address)) {
                iter->Next();
                continue;
            }

            if (address.first != DB_REFERRALS) {
                iter->Next();
                continue;
            }

            MutableReferral referral;
            if (!iter->GetValue(referral)) {
                return false;
            }

            addresses.push_back({referral.addressType, referral.GetAddress()});

            iter->Next();
        }

        debug("Confirming %d pre daedalus addresses", addresses.size());
        std::sort(addresses.begin(), addresses.end(),
                [](const AddressPair& a, const AddressPair& b) {
                return a.second < b.second;
                });

        for(const auto& addr : addresses) {
            debug("\tConfirming %s address", CMeritAddress{addr.first, addr.second}.ToString());
            if (!UpdateConfirmation(addr.first, addr.second, 1)) {
                return false;
            }
        }

        //Mark state in DB that all addresses before daedalus have been confirmed.
        if (!m_db.Write(DB_PRE_DAEDALUS_CONFIRMED, true)) {
            return false;
        }

        return true;
    }

    bool ReferralsViewDB::AreAllPreDaedalusAddressesConfirmed() const
    {
        return m_db.Exists(DB_PRE_DAEDALUS_CONFIRMED);
    }

    uint64_t ReferralsViewDB::GetTotalConfirmations() const
    {
        uint64_t total = 0;
        m_db.Read(DB_CONFIRMATION_TOTAL, total);
        return total;
    }

    MaybeConfirmedAddress ReferralsViewDB::GetConfirmation(uint64_t idx) const
    {
        auto total = GetTotalConfirmations();
        if (idx >= total) {
            return MaybeConfirmedAddress{};
        }

        ConfirmationVal val;
        if (!m_db.Read(std::make_pair(DB_CONFIRMATION_IDX, idx), val)) {
            return MaybeConfirmedAddress{};
        }

        ConfirmationPair pair{0,0};

        if(!m_db.Read(
                    std::make_pair(DB_CONFIRMATION, val.second),
                    pair)) {
            return MaybeConfirmedAddress{};
        }

        return MaybeConfirmedAddress{{val.first, val.second, pair.second}};
    }

} //namespace referral
