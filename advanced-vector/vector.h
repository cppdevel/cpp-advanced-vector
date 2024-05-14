#pragma once

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <new>
#include <memory>
#include <utility>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept
        : buffer_(std::exchange(other.buffer_, nullptr))
        , capacity_(std::exchange(other.capacity_, 0))
    {
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    Vector() = default;

    explicit Vector(size_t size)
        : data_(size)
        , size_(size)
    {
        std::uninitialized_value_construct_n(begin(), size);
    }

    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)
    {
        std::uninitialized_copy_n(other.begin(), size_, begin());
    }

    Vector(Vector&& other) noexcept
        : data_(std::move(other.data_))
        , size_(std::exchange(other.size_, 0))
    {
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > Capacity()) {
                Vector copied_rhs(rhs);
                Swap(copied_rhs);
            }
            else {
                size_t pos = 0;
                for (; pos < std::min(size_, rhs.size_); ++pos) {
                    data_[pos] = rhs.data_[pos];
                }
                if (size_ > rhs.size_) {
                    std::destroy_n(&data_[rhs.size_], size_ - rhs.size_);
                }
                else {
                    std::uninitialized_copy_n(&rhs.data_[pos], rhs.size_ - size_, &data_[pos]);
                }
                size_ = rhs.size_;
            }
        }
        return *this;
    }

    Vector& operator=(Vector&& rhs) noexcept {
        if (this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }

    ~Vector() {
        std::destroy_n(begin(), size_);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }
        RawMemory<T> new_data(new_capacity);
        FillNewData(new_data, size_);
        std::destroy_n(begin(), size_);
        data_.Swap(new_data);
    }

    void Resize(size_t new_size) {
        if (new_size < size_) {
            std::destroy_n(begin() + new_size, size_ - new_size);
        }
        else {
            if (new_size > Capacity()) {
                size_t new_capacity = std::max(Capacity() * 2, new_size);
                Reserve(new_capacity);
            }
            std::uninitialized_value_construct_n(end(), new_size - size_);
        }
        size_ = new_size;
    }

    template <typename T1>
    void PushBack(T1&& value) {
        EmplaceBack(std::forward<T1>(value));
    }

    void PopBack() noexcept {
        assert(size_);
        std::destroy_at(end() - 1);
        --size_;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *(Emplace(end(), std::forward<Args>(args)...));
    }

    [[nodiscard]] size_t Size() const noexcept {
        return size_;
    }

    [[nodiscard]] size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    iterator begin() noexcept {
        return data_.GetAddress();
    }

    iterator end() noexcept {
        return data_.GetAddress() + size_;
    }

    const_iterator begin() const noexcept {
        return cbegin();
    }

    const_iterator end() const noexcept {
        return cend();
    }

    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }

    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_;
    }

    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        size_t iterator_pos = pos - begin();
        if (size_ == Capacity()) {
            InsertWithoutRelocation(iterator_pos, pos, std::forward<Args>(args)...);
        }
        else {
            InsertWithRelocation(iterator_pos, pos, std::forward<Args>(args)...);
        }
        ++size_;
        return begin() + iterator_pos;
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        assert(pos >= cbegin() && pos < cend());
        size_t iterator_pos = pos - cbegin();
        std::move(begin() + iterator_pos + 1, end(), begin() + iterator_pos);
        PopBack();
        return begin() + iterator_pos;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void FillNewData(RawMemory<T>& new_data, size_t pos) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin(), pos, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(begin(), pos, new_data.GetAddress());
        }
    }

    template <typename... Args>
    void InsertWithRelocation(size_t iterator_pos, const_iterator pos, Args&&... args) {
        try {
            if (pos != end()) {
                T temporary_obj(std::forward<Args>(args)...);
                std::uninitialized_move_n(end() - 1, 1, end());
                std::move_backward(data_ + iterator_pos, end() - 1, end());
                data_[iterator_pos] = std::move(temporary_obj);
            }
            else {
                new (end()) T(std::forward<Args>(args)...);
            }
        }
        catch (...) {
            operator delete (end());
            throw;
        }
    }

    template <typename... Args>
    void InsertWithoutRelocation(size_t iterator_pos, const_iterator pos, Args&&... args) {
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        new (new_data.GetAddress() + iterator_pos) T(std::forward<Args>(args)...);
        FillNewData(new_data, iterator_pos);
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(begin() + iterator_pos, size_ - iterator_pos, new_data.GetAddress() + iterator_pos + 1);
        }
        else {
            std::uninitialized_copy_n(begin() + iterator_pos, size_ - iterator_pos, new_data.GetAddress() + iterator_pos + 1);
        }
        std::destroy_n(begin(), size_);
        data_.Swap(new_data);
    }
};