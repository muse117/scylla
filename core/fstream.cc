/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */

#include "fstream.hh"
#include <malloc.h>

class file_data_source_impl : public data_source_impl {
    lw_shared_ptr<file> _file;
    uint64_t _pos;
    size_t _buffer_size;
public:
    file_data_source_impl(lw_shared_ptr<file> f, uint64_t pos, size_t buffer_size)
            : _file(std::move(f)), _pos(pos), _buffer_size(buffer_size) {}
    virtual future<temporary_buffer<char>> get() override {
        // must align allocation for dma
        auto p = ::memalign(std::min<size_t>(_buffer_size, 4096), _buffer_size);
        if (!p) {
            throw std::bad_alloc();
        }
        auto q = static_cast<char*>(p);
        temporary_buffer<char> buf(q, _buffer_size, make_free_deleter(p));
        auto old_pos = _pos;
        // dma_read needs to be aligned. It doesn't have to be page-aligned,
        // though, and we could get away with something much smaller. However, if
        // we start reading in things outside page boundaries, we will end up with
        // various pages around, some of them with overlapping ranges. Those would
        // be very challenging to cache.
        old_pos &= ~4095;
        auto front = _pos - old_pos;
        _pos += _buffer_size - front;
        return _file->dma_read(old_pos, q, _buffer_size).then(
                [buf = std::move(buf), front] (size_t size) mutable {
            buf.trim(size);
            buf.trim_front(front);
            return make_ready_future<temporary_buffer<char>>(std::move(buf));
        });
    }
};

class file_data_source : public data_source {
public:
    file_data_source(lw_shared_ptr<file> f, uint64_t offset, size_t buffer_size)
        : data_source(std::make_unique<file_data_source_impl>(
                std::move(f), offset, buffer_size)) {}
};

input_stream<char> make_file_input_stream(
        lw_shared_ptr<file> f, uint64_t offset, size_t buffer_size) {
    return input_stream<char>(file_data_source(std::move(f), offset, buffer_size));
}
