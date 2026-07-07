#pragma once

namespace Microsoft {
namespace WRL {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    ~ComPtr() { Reset(); }

    T* Get() const { return ptr_; }
    T** GetAddressOf() { return &ptr_; }
    T** operator&() { return ReleaseAndGetAddressOf(); }
    T** ReleaseAndGetAddressOf() {
        Reset();
        return &ptr_;
    }

    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    template <typename U>
    long As(ComPtr<U>* out) const {
        return ptr_->QueryInterface(__uuidof(U),
            reinterpret_cast<void**>(out->ReleaseAndGetAddressOf()));
    }

    template <typename U>
    long As(U** out) const {
        if (out) *out = nullptr;
        return ptr_->QueryInterface(__uuidof(U),
            reinterpret_cast<void**>(out));
    }

private:
    T* ptr_ = nullptr;
};

} // namespace WRL
} // namespace Microsoft
