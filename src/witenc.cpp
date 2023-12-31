#include <iostream>
#include <random>
#include "witenc.hpp"

using std::string;
using std::vector;

using namespace bls;

std::random_device device;
std::default_random_engine generator(device());
std::uniform_int_distribution<uint8_t> byte_distribution(0, 255);

namespace witenc {
    CipherText Scheme::Encrypt(const G1& pk, const bytes& tag, const bytes& msg) {
        CipherText ct;
        blst_scalar r1 = BuildC1(ct);
        GT r2 = BuildC2(ct, pk, r1, tag);
        BuildC3(ct, r2, msg);

        return ct;
    }

    bytes Scheme::Decrypt(const G2& sig, const CipherText& ctxt) {
        GT r2 = RetrieveGT(ctxt, sig);
        bytes hash = HashGT(r2);
        bytes msg = UnmaskMessage(ctxt.c3, hash);

        return msg;
    }

    blst_scalar Scheme::BuildC1(CipherText& ct) {
        blst_scalar r1 = Helpers::RandomScalar();
        ct.c1 = r1 * G1::Generator();
        ct.c1.CheckValid();

        return r1;
    }

    GT Scheme::BuildC2(CipherText& ct, const G1& pk, const blst_scalar r1, const bytes& tag) {
        GT r2 = Helpers::RandomGT();

        G2 g2_map = G2::FromMessage(
            tag, 
            (const uint8_t*)BasicSchemeMPL::CIPHERSUITE_ID.c_str(), 
            BasicSchemeMPL::CIPHERSUITE_ID.length()
        );

        GT pair = pk.Pair(r1 * g2_map);
        GT c2 = pair * r2;
        ct.c2 = c2.Serialize();

        return r2;
    }

    void Scheme::BuildC3(CipherText& ct, const GT& r2, const bytes& msg) {
        bytes hash = HashGT(r2);
        ct.c3 = Scheme::MaskMessage(msg, hash);
    }

    bytes Scheme::HashGT(const GT& gt) {
        bytes serialized_gt = gt.Serialize();
        bytes hash(32);
        Util::Hash256(hash.data(), serialized_gt.data(), serialized_gt.size());

        return hash;
    }

    GT Scheme::RetrieveGT(const CipherText& ct, const G2& sig) {
        GT c2 = GT::FromByteVector(ct.c2);
        GT pair = ct.c1.Pair(sig);
        return c2 / pair;
    }

    bytes Scheme::MaskMessage(const bytes& msg, const bytes& hash) {
        return OTP::Encrypt(hash, msg);
    }

    bytes Scheme::UnmaskMessage(const bytes& c3, const bytes& hash) {
        return OTP::Decrypt(hash, c3);
    }

    CipherText::CipherText() {}

    string CipherText::ToHexStr() const {
        this->Validate();
        return Util::HexStr(this->Serialize());
    }

    CipherText CipherText::FromHexStr(const string& str) {
        vector<std::uint8_t> bytes = Util::HexToBytes(str);
        return CipherText::Deserialize(bytes);
    }

    void CipherText::Validate() const {
        if (!this->c1.IsValid()) {
            throw std::invalid_argument("Bad Ciphertext: Invalid c1");
        }

        if (this->c2.empty() || this->c2.size() != GT::SIZE) {
            throw std::invalid_argument("Bad Ciphertext: Invalid c2");
        }

        if (this->c3.empty()) {
            throw std::invalid_argument("Bad Ciphertext: Empty c3");
        }
    }

    bytes CipherText::Serialize() const {
        bytes serialized;
        bytes c1 = this->c1.Serialize();
        serialized.insert(serialized.end(), c1.begin(), c1.end());
        serialized.insert(serialized.end(), this->c2.begin(), this->c2.end());
        serialized.insert(serialized.end(), this->c3.begin(), this->c3.end());

        return serialized;
    }

    CipherText CipherText::Deserialize(bytes& bytes) {
        CipherText ct;
        size_t c1_size = G1::SIZE;
        size_t c2_size = GT::SIZE;
        size_t c3_size = bytes.size() - c1_size - c2_size;

        if (bytes.size() < (c1_size + c2_size + c3_size)) {
            throw std::invalid_argument("Bad Ciphertext: Invalid bytes");
        }

        ct.c1 = G1::FromByteVector(vector<uint8_t>(bytes.begin(), bytes.begin() + c1_size));
        ct.c2 = vector<uint8_t>(bytes.begin() + c1_size, bytes.begin() + c1_size + c2_size);
        ct.c3 = vector<uint8_t>(bytes.begin() + c1_size + c2_size, bytes.begin() + c1_size + c2_size + c3_size);

        ct.Validate();

        return ct;
    }

    bool operator==(CipherText const& a, CipherText const& b) {
        return a.c1 == b.c1 && a.c2 == b.c2 && a.c3 == b.c3;
    }

    bytes OTP::Exec(const bytes& key, const bytes& msg) {
        int bs = key.size();
        int msg_size = msg.size();
        int num_blocks = msg_size / bs;
        int remainder = msg_size % key.size();
        num_blocks += remainder > 0 ? 1 : 0;

        bytes result(num_blocks * bs, 0x00);
        bytes hash(32);

        for (int j = 0; j < num_blocks; j++) {
            //create new key and append a byte counter
            bytes new_key(key);
            bytes four_bytes(4);
            Util::IntToFourBytes(four_bytes.data(), j);
            new_key.insert(new_key.end(), four_bytes.begin(), four_bytes.end());
            // Hash the new key
            Util::Hash256(hash.data(), new_key.data(), new_key.size());

            for (int i = 0; i < bs; i++) {
                int idx = j * bs + i;

                if (idx < msg_size) {
                    result[idx] = msg[idx] ^ hash[i];
                } else {
                    result[idx] = 0x00 ^ hash[i];
                }
            }
        }

        return result;
    }

    bytes OTP::Encrypt(const bytes& key, const bytes& msg) {
        return OTP::Exec(key, msg);
    }

    bytes OTP::Decrypt(const bytes& key, const bytes& ct) {
        bytes msg = OTP::Exec(key, ct);
        Helpers::RemoveTrailingZeroes(msg);
        return msg;
    }

    void Helpers::RemoveTrailingZeroes(bytes &bytes) {
        int index = -1;

        for (int i = bytes.size() - 1; i >= 0; i--) {
            if (bytes[i] == 0x00) {
                index = i;
            } else {
                break;
            }
        }

        if (index > -1) {
            bytes.erase(bytes.begin() + index, bytes.end());
        }
    }

    blst_scalar Helpers::RandomScalar() {
        bytes random_bytes(32);

        for (int i = 0; i < 32; i++) {
            random_bytes[i] = byte_distribution(generator);
        }

        blst_scalar* scalar = Util::SecAlloc<blst_scalar>(1);
        blst_scalar_from_bendian(scalar, random_bytes.data());
        blst_scalar s = *scalar;
        Util::SecFree(scalar);

        return s;
    }

    GT Helpers::RandomGT() {
        blst_scalar s1 = RandomScalar();
        G1 g1 = s1 * G1::Generator();
        
        blst_scalar s2 = RandomScalar();
        G2 g2 = s2 * G2::Generator();

        return g1.Pair(g2);
    }
}
