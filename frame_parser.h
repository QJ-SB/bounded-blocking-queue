#pragma once

#include <cstddef>
#include <vector>

class FrameParser {
public:
    std::vector<std::vector<std::byte>> feed(const std::byte* data,
                                             std::size_t size);

    void finish();

private:
    // Bytes received but not yet consumed.
    std::vector<std::byte> pending_bytes_;
};
