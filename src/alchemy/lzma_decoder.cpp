#include "alchemy/lzma_decoder.h"

#include <algorithm>
#include <array>

namespace skylanders::alchemy {
namespace {

constexpr int kNumBitModelTotalBits = 11;
constexpr uint32_t kBitModelTotal = 1u << kNumBitModelTotalBits;
constexpr int kNumMoveBits = 5;
constexpr uint16_t kProbInit = kBitModelTotal / 2;
constexpr int kNumStates = 12;
constexpr int kNumPosBitsMax = 4;
constexpr int kNumLenToPosStates = 4;
constexpr int kNumAlignBits = 4;
constexpr int kStartPosModelIndex = 4;
constexpr int kEndPosModelIndex = 14;
constexpr int kNumFullDistances = 1 << (kEndPosModelIndex >> 1);
constexpr int kMatchMinLen = 2;

class RangeDecoder {
 public:
  RangeDecoder(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool Init() {
    if (Next() != 0) return false;
    for (int i = 0; i < 4; ++i) code_ = (code_ << 8) | Next();
    return code_ != range_ && !overrun_;
  }

  uint32_t DecodeDirect(int bits) {
    uint32_t result = 0;
    while (bits--) {
      range_ >>= 1;
      code_ -= range_;
      const uint32_t t = 0u - (code_ >> 31);
      code_ += range_ & t;
      if (code_ == range_) corrupted_ = true;
      Normalize();
      result = (result << 1) + (t + 1);
    }
    return result;
  }

  uint32_t DecodeBit(uint16_t& prob) {
    uint32_t v = prob;
    const uint32_t bound = (range_ >> kNumBitModelTotalBits) * v;
    uint32_t symbol;
    if (code_ < bound) {
      v += (kBitModelTotal - v) >> kNumMoveBits;
      range_ = bound;
      symbol = 0;
    } else {
      v -= v >> kNumMoveBits;
      code_ -= bound;
      range_ -= bound;
      symbol = 1;
    }
    prob = uint16_t(v);
    Normalize();
    return symbol;
  }

  bool failed() const { return corrupted_ || overrun_; }

 private:
  uint8_t Next() {
    if (pos_ >= size_) {
      overrun_ = true;
      return 0;
    }
    return data_[pos_++];
  }

  void Normalize() {
    if (range_ < (1u << 24)) {
      range_ <<= 8;
      code_ = (code_ << 8) | Next();
    }
  }

  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
  uint32_t range_ = 0xFFFFFFFF;
  uint32_t code_ = 0;
  bool corrupted_ = false;
  bool overrun_ = false;
};

// probs[1 .. 2^bits) form the tree.
uint32_t BitTree(uint16_t* probs, int bits, RangeDecoder& rc) {
  uint32_t m = 1;
  for (int i = 0; i < bits; ++i) m = (m << 1) + rc.DecodeBit(probs[m]);
  return m - (1u << bits);
}

uint32_t BitTreeReverse(uint16_t* probs, int bits, RangeDecoder& rc) {
  uint32_t m = 1, symbol = 0;
  for (int i = 0; i < bits; ++i) {
    const uint32_t bit = rc.DecodeBit(probs[m]);
    m = (m << 1) + bit;
    symbol |= bit << i;
  }
  return symbol;
}

struct LenDecoder {
  uint16_t choice = kProbInit;
  uint16_t choice2 = kProbInit;
  uint16_t low[1 << kNumPosBitsMax][1 << 3];
  uint16_t mid[1 << kNumPosBitsMax][1 << 3];
  uint16_t high[1 << 8];

  LenDecoder() {
    std::fill(&low[0][0], &low[0][0] + sizeof(low) / sizeof(uint16_t), kProbInit);
    std::fill(&mid[0][0], &mid[0][0] + sizeof(mid) / sizeof(uint16_t), kProbInit);
    std::fill(std::begin(high), std::end(high), kProbInit);
  }

  uint32_t Decode(RangeDecoder& rc, uint32_t pos_state) {
    if (!rc.DecodeBit(choice)) return BitTree(low[pos_state], 3, rc);
    if (!rc.DecodeBit(choice2)) return 8 + BitTree(mid[pos_state], 3, rc);
    return 16 + BitTree(high, 8, rc);
  }
};

}  // namespace

std::optional<std::vector<uint8_t>> LzmaDecode(const uint8_t* data, size_t size,
                                               size_t unpacked_size) {
  if (size < 5) return std::nullopt;
  uint32_t d = data[0];
  if (d >= 9 * 5 * 5) return std::nullopt;
  const uint32_t lc = d % 9;
  d /= 9;
  const uint32_t lp = d % 5;
  const uint32_t pb = d / 5;

  std::vector<uint16_t> literal_probs(size_t(0x300) << (lc + lp), kProbInit);
  std::array<uint16_t, kNumStates << kNumPosBitsMax> is_match;
  std::array<uint16_t, kNumStates> is_rep, is_rep_g0, is_rep_g1, is_rep_g2;
  std::array<uint16_t, kNumStates << kNumPosBitsMax> is_rep0_long;
  uint16_t pos_slot[kNumLenToPosStates][1 << 6];
  std::array<uint16_t, 1 + kNumFullDistances - kEndPosModelIndex> pos_decoders;
  std::array<uint16_t, 1 << kNumAlignBits> align;
  for (auto* a : {&is_rep, &is_rep_g0, &is_rep_g1, &is_rep_g2}) a->fill(kProbInit);
  is_match.fill(kProbInit);
  is_rep0_long.fill(kProbInit);
  std::fill(&pos_slot[0][0], &pos_slot[0][0] + sizeof(pos_slot) / sizeof(uint16_t), kProbInit);
  pos_decoders.fill(kProbInit);
  align.fill(kProbInit);
  LenDecoder len_decoder, rep_len_decoder;

  RangeDecoder rc(data + 5, size - 5);
  if (!rc.Init()) return std::nullopt;

  std::vector<uint8_t> out;
  out.reserve(unpacked_size);
  uint32_t rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
  uint32_t state = 0;
  const uint32_t pb_mask = (1u << pb) - 1;
  const uint32_t lp_mask = (1u << lp) - 1;

  while (out.size() < unpacked_size) {
    if (rc.failed()) return std::nullopt;
    const uint32_t pos_state = uint32_t(out.size()) & pb_mask;

    if (!rc.DecodeBit(is_match[(state << kNumPosBitsMax) + pos_state])) {
      // Literal, coded against the byte at rep0 after a match.
      const uint32_t prev = out.empty() ? 0 : out.back();
      const uint32_t lit_state = ((uint32_t(out.size()) & lp_mask) << lc) + (prev >> (8 - lc));
      uint16_t* probs = &literal_probs[size_t(0x300) * lit_state];
      uint32_t symbol = 1;
      if (state >= 7) {
        if (rep0 >= out.size()) return std::nullopt;
        uint32_t match_byte = out[out.size() - rep0 - 1];
        do {
          const uint32_t match_bit = (match_byte >> 7) & 1;
          match_byte <<= 1;
          const uint32_t bit = rc.DecodeBit(probs[((1 + match_bit) << 8) + symbol]);
          symbol = (symbol << 1) | bit;
          if (match_bit != bit) break;
        } while (symbol < 0x100);
      }
      while (symbol < 0x100) symbol = (symbol << 1) | rc.DecodeBit(probs[symbol]);
      out.push_back(uint8_t(symbol - 0x100));
      state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
      continue;
    }

    uint32_t len;
    if (rc.DecodeBit(is_rep[state])) {
      if (out.empty()) return std::nullopt;
      if (!rc.DecodeBit(is_rep_g0[state])) {
        if (!rc.DecodeBit(is_rep0_long[(state << kNumPosBitsMax) + pos_state])) {
          // Short rep: one byte from rep0.
          state = state < 7 ? 9 : 11;
          if (rep0 >= out.size()) return std::nullopt;
          out.push_back(out[out.size() - rep0 - 1]);
          continue;
        }
      } else {
        uint32_t dist;
        if (!rc.DecodeBit(is_rep_g1[state])) {
          dist = rep1;
        } else {
          if (!rc.DecodeBit(is_rep_g2[state])) {
            dist = rep2;
          } else {
            dist = rep3;
            rep3 = rep2;
          }
          rep2 = rep1;
        }
        rep1 = rep0;
        rep0 = dist;
      }
      len = rep_len_decoder.Decode(rc, pos_state);
      state = state < 7 ? 8 : 11;
    } else {
      rep3 = rep2;
      rep2 = rep1;
      rep1 = rep0;
      len = len_decoder.Decode(rc, pos_state);
      state = state < 7 ? 7 : 10;
      // Distance.
      const uint32_t len_state = std::min<uint32_t>(len, kNumLenToPosStates - 1);
      const uint32_t slot = BitTree(pos_slot[len_state], 6, rc);
      if (slot < kStartPosModelIndex) {
        rep0 = slot;
      } else {
        const int direct_bits = int(slot >> 1) - 1;
        uint32_t dist = (2 | (slot & 1)) << direct_bits;
        if (slot < kEndPosModelIndex) {
          dist += BitTreeReverse(pos_decoders.data() + dist - slot, direct_bits, rc);
        } else {
          dist += rc.DecodeDirect(direct_bits - kNumAlignBits) << kNumAlignBits;
          dist += BitTreeReverse(align.data(), kNumAlignBits, rc);
        }
        rep0 = dist;
      }
      if (rep0 == 0xFFFFFFFF) break;  // end marker
      if (rep0 >= out.size()) return std::nullopt;
    }

    len += kMatchMinLen;
    const size_t copy = std::min<size_t>(len, unpacked_size - out.size());
    const size_t from = out.size() - rep0 - 1;
    for (size_t i = 0; i < copy; ++i) out.push_back(out[from + i]);
  }
  // The stream may end right at the last symbol, so running out of input while finishing it is
  // fine; running out earlier was caught at the top of the loop.
  if (out.size() != unpacked_size) return std::nullopt;
  return out;
}

}  // namespace skylanders::alchemy
