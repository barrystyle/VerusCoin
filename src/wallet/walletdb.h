// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef BITCOIN_WALLET_WALLETDB_H
#define BITCOIN_WALLET_WALLETDB_H

#include "amount.h"
#include "wallet/db.h"
#include "key.h"
#include "keystore.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

#include <list>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

class CAccount;
class CAccountingEntry;
struct CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWallet;
class CWalletTx;
class uint160;
class uint256;

/** Error statuses for the wallet database */
enum DBErrors
{
    DB_LOAD_OK,
    DB_LOAD_CRYPTED,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE
};

/* simple hd chain data model */
class CHDChain
{
public:
    static const int VERSION_HD_BASE = 1;
    static const int CURRENT_VERSION = VERSION_HD_BASE;
    int nVersion;
    uint256 seedFp;
    int64_t nCreateTime; // 0 means unknown
    uint32_t saplingAccountCounter;

    CHDChain() { SetNull(); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(seedFp);
        READWRITE(nCreateTime);
        READWRITE(saplingAccountCounter);
    }

    void SetNull()
    {
        nVersion = CHDChain::CURRENT_VERSION;
        seedFp.SetNull();
        nCreateTime = 0;
        saplingAccountCounter = 0;
    }
};

class CKeyMetadata
{
public:
    static const int VERSION_BASIC=1;
    static const int VERSION_WITH_HDDATA=10;
    static const int CURRENT_VERSION=VERSION_WITH_HDDATA;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    std::string hdKeypath; //optional HD/zip32 keypath
    uint256 seedFp;

    CKeyMetadata()
    {
        SetNull();
    }
    CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(nCreateTime);
        if (this->nVersion >= VERSION_WITH_HDDATA)
        {
            READWRITE(hdKeypath);
            READWRITE(seedFp);
        }
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        hdKeypath.clear();
        seedFp.SetNull();
    }
};

/** Access to the wallet database (wallet.dat) */
class CWalletDB : public CDB
{
public:
    CWalletDB(const std::string& strFilename, const char* pszMode = "r+", bool fFlushOnClose = true) : CDB(strFilename, pszMode, fFlushOnClose)
    {
    }

    template <typename K, typename T>
    bool WriteTxn(const K& key, const T& value, std::string calling, bool fOverwrite = true)
    {

        LOCK(bitdb.cs_db);
        bool txnWrite = false;
        int retries = 0;

        while(!txnWrite) {
            //Writing transaction to the database
            if(!TxnBegin()) {
                LogPrintf("%s: Failed to begin txn, will retry.\n", calling);
                TxnAbort();
            } else {
                TxnSetTimeout();
                txnWrite = Write(key, value, fOverwrite);
                if (!txnWrite) {
                    LogPrintf("%s: Failed to write txn, will retry.\n", calling);
                    TxnAbort();
                } else {
                    if(!TxnCommit()) {
                      LogPrintf("%s: Failed to commit txn, warning.\n", calling);
                    }
                }
            }

            if (!txnWrite) {
              TxnAbort();
              MilliSleep(500);
              retries++;
            }

            if (retries > 3) {
              LogPrintf("%s Failed!!! Retry, attempts #%d.\n", calling, retries - 1);
              return false;
            }
        }
        return true;
    }

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool EraseName(const std::string& strAddress);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool ErasePurpose(const std::string& strAddress);

    bool WriteTx(uint256 hash, const CWalletTx& wtx);
    bool EraseTx(uint256 hash);

    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata &keyMeta);
    bool WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata &keyMeta);
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CScript& redeemScript);

    bool WriteWatchOnly(const CScript &script);
    bool EraseWatchOnly(const CScript &script);

    //Write crypted status of the wallet
    bool WriteIsCrypted(const bool &crypted);

    bool WriteIdentity(const CIdentityMapKey &mapKey, const CIdentityMapValue &id);
    bool EraseIdentity(const CIdentityMapKey &mapKey);

    bool WriteBestBlock(const CBlockLocator& locator);
    bool ReadBestBlock(CBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool WriteDefaultKey(const CPubKey& vchPubKey);

    bool WriteWitnessCacheSize(int64_t nWitnessCacheSize);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    bool ReadAccount(const std::string& strAccount, CAccount& account);
    bool WriteAccount(const std::string& strAccount, const CAccount& account);

    /// Write destination data key,value tuple to database
    bool WriteDestData(const std::string &address, const std::string &key, const std::string &value);
    /// Erase destination data tuple from wallet database
    bool EraseDestData(const std::string &address, const std::string &key);

    bool WriteAccountingEntry(const CAccountingEntry& acentry);
    CAmount GetAccountCreditDebit(const std::string& strAccount);
    void ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& acentries);

    DBErrors ReorderTransactions(CWallet* pwallet);
    DBErrors InitalizeCryptedLoad(CWallet* pwallet);
    DBErrors LoadCryptedSeedFromDB(CWallet* pwallet);
    DBErrors LoadWallet(CWallet* pwallet);
    DBErrors FindWalletTxToZap(CWallet* pwallet, std::vector<uint256>& vTxHash, std::vector<CWalletTx>& vWtx);
    DBErrors ZapWalletTx(CWallet* pwallet, std::vector<CWalletTx>& vWtx);
    static bool Recover(CDBEnv& dbenv, const std::string& filename, bool fOnlyKeys);
    static bool Recover(CDBEnv& dbenv, const std::string& filename);

    bool WriteHDSeed(const HDSeed& seed);
    bool WriteCryptedHDSeed(const uint256& seedFp, const std::vector<unsigned char>& vchCryptedSecret);
    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);

    //Wrtie the address, ivk and path of diversified address to the wallet
    bool WriteSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);
    bool WriteCryptedSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const uint256 chash,
        const std::vector<unsigned char> &vchCryptedSecret);

    //Write the last used diversifier and ivk used
    bool WriteLastDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);
    bool WriteLastCryptedDiversifierUsed(
        const uint256 chash,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const std::vector<unsigned char> &vchCryptedSecret);

    /// Write spending key to wallet database, where key is payment address and value is spending key.
    bool WriteZKey(const libzcash::SproutPaymentAddress& addr, const libzcash::SproutSpendingKey& key, const CKeyMetadata &keyMeta);
    bool WriteSaplingZKey(const libzcash::SaplingIncomingViewingKey &ivk,
                          const libzcash::SaplingExtendedSpendingKey &key,
                          const CKeyMetadata  &keyMeta);
    bool WriteSaplingPaymentAddress(const libzcash::SaplingPaymentAddress &addr,
                                    const libzcash::SaplingIncomingViewingKey &ivk);
    bool WriteCryptedZKey(const libzcash::SproutPaymentAddress & addr,
                          const libzcash::ReceivingKey & rk,
                          const std::vector<unsigned char>& vchCryptedSecret,
                          const CKeyMetadata &keyMeta);
    bool WriteCryptedSaplingZKey(const libzcash::SaplingExtendedFullViewingKey &extfvk,
                          const std::vector<unsigned char>& vchCryptedSecret,
                          const CKeyMetadata &keyMeta);
    bool WriteCryptedSaplingExtendedFullViewingKey(
                          const libzcash::SaplingExtendedFullViewingKey &extfvk,
                          const std::vector<unsigned char>& vchCryptedSecret);

    bool WriteSproutViewingKey(const libzcash::SproutViewingKey &vk);
    bool EraseSproutViewingKey(const libzcash::SproutViewingKey &vk);
    bool WriteSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk);
    bool EraseSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk);

private:
    CWalletDB(const CWalletDB&);
    void operator=(const CWalletDB&);

    bool WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry);
};

bool BackupWallet(const CWallet& wallet, const std::string& strDest);
void ThreadFlushWalletDB(const std::string& strFile);

#endif // BITCOIN_WALLET_WALLETDB_H
