#include "atum/core/packet.hpp"
#include <cstring>

namespace atum {

Packet::Packet(const Packet& other)
    : borrowed_data_(other.borrowed_data_)
    , borrowed_size_(other.borrowed_size_)
    , is_owned_(other.is_owned_)
    , pts_(other.pts_)
    , dts_(other.dts_)
    , duration_(other.duration_)
    , stream_index_(other.stream_index_)
    , flags_(other.flags_) {
    if (other.is_owned_) {
        owned_data_ = other.owned_data_;
    }
}

Packet& Packet::operator=(const Packet& other) {
    if (this == &other) return *this;

    borrowed_data_ = other.borrowed_data_;
    borrowed_size_ = other.borrowed_size_;
    is_owned_ = other.is_owned_;
    pts_ = other.pts_;
    dts_ = other.dts_;
    duration_ = other.duration_;
    stream_index_ = other.stream_index_;
    flags_ = other.flags_;

    if (other.is_owned_) {
        owned_data_ = other.owned_data_;
    } else {
        owned_data_.clear();
    }
    return *this;
}

Packet Packet::allocate(size_t size) {
    Packet pkt;
    pkt.owned_data_.resize(size);
    pkt.is_owned_ = true;
    return pkt;
}

Packet Packet::from_borrowed(const uint8_t* data, size_t size) noexcept {
    Packet pkt;
    pkt.borrowed_data_ = data;
    pkt.borrowed_size_ = size;
    pkt.is_owned_ = false;
    return pkt;
}

Packet Packet::from_bytes(std::span<const uint8_t> bytes) {
    Packet pkt;
    pkt.owned_data_.assign(bytes.begin(), bytes.end());
    pkt.is_owned_ = true;
    return pkt;
}

void Packet::resize(size_t new_size) {
    ensure_owned();
    owned_data_.resize(new_size);
}

void Packet::ensure_owned() {
    if (is_owned_) return;
    if (borrowed_data_ && borrowed_size_ > 0) {
        owned_data_.assign(borrowed_data_, borrowed_data_ + borrowed_size_);
    } else {
        owned_data_.clear();
    }
    borrowed_data_ = nullptr;
    borrowed_size_ = 0;
    is_owned_ = true;
}

void Packet::clear() noexcept {
    owned_data_.clear();
    borrowed_data_ = nullptr;
    borrowed_size_ = 0;
    is_owned_ = false;
    pts_ = NOPTS;
    dts_ = NOPTS;
    duration_ = 0;
    stream_index_ = -1;
    flags_ = PacketFlags::None;
}

} // namespace atum
