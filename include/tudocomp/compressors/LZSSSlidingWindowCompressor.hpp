#pragma once

#include <tudocomp/Compressor.hpp>
#include <tudocomp/Literal.hpp>
#include <tudocomp/Range.hpp>
#include <tudocomp/Tags.hpp>
#include <tudocomp/util.hpp>

#include <tudocomp/ds/RingBuffer.hpp>

#include <tudocomp/compressors/lzss/Factor.hpp>
#include <tudocomp/decompressors/LZSSDecompressor.hpp>

#include <tudocomp_stat/StatPhase.hpp>

namespace tdc {

/// Computes the LZ77 factorization of the input by moving a sliding window
/// over it in which redundant phrases will be looked for.
template<typename lzss_coder_t>
class LZSSSlidingWindowCompressor : public Compressor {

private:
    size_t m_threshold;
    size_t m_window;

public:
    inline static Meta meta() {
        Meta m(Compressor::type_desc(), "lzss",
            "Computes the LZSS factorization of the input using a "
            "sliding window.");
        m.param("coder", "The output encoder.")
            .strategy<lzss_coder_t>(TypeDesc("lzss_coder"));
        m.param("window", "The sliding window size").primitive(16);
        m.param("threshold", "The minimum factor length.").primitive(2);
        m.inherit_tag<lzss_coder_t>(tags::lossy);
        return m;
    }

    /// Default constructor (not supported).
    inline LZSSSlidingWindowCompressor() = delete;

    /// Construct the class with an environment.
    inline LZSSSlidingWindowCompressor(Config&& c) : Compressor(std::move(c)) {
        m_threshold = this->config().param("threshold").as_uint();
        m_window = this->config().param("window").as_uint();
    }

    /// \copydoc Compressor::compress
    inline virtual void compress(Input& input, Output& output) override {
        // initialize encoder
        auto coder = lzss_coder_t(config().sub_config("coder"))
            .encoder(output, NoLiterals());

        coder.factor_length_range(Range(m_threshold, 2 * m_window));
        coder.encode_header();

        // allocate window and lookahead buffer
        RingBuffer<uliteral_t> window(m_window), ahead(m_window);

        // open stream
        auto ins = input.as_stream();
        decltype(ins)::int_type c;

        // initialize lookahead buffer
        while(!ahead.full() && (c = ins.get()) >= 0) {
            ahead.push_back(uliteral_t(c));
        }

        // factorize
        size_t i = 0; // all symbols before i have already been factorized
        while(!ahead.empty()) {
            auto ahead_dbg = ahead.dump();
            auto window_dbg = window.dump();

            // look for longest prefix of ahead in window
            size_t flen = 1, fsrc = SIZE_MAX;
            size_t pos = 0;

            for(auto it = window.begin(); it != window.end(); it++) {
                if(*it == ahead.peek_front()) {
                    // at least one character matches, find factor length
                    // by comparing additional characters
                    size_t len = 1;

                    auto it_window = it + 1;
                    auto it_ahead = ahead.begin() + 1;

                    while(it_window != window.end() &&
                          it_ahead != ahead.end() &&
                          *it_window == *it_ahead) {

                        ++it_window;
                        ++it_ahead;
                        ++len;
                    }

                    if(it_window == window.end() && it_ahead != ahead.end()) {
                        // we looked until the end of the window, but there
                        // are more characters in the lookahead buffer
                        // -> test for overlapping factor!
                        auto it_overlap = ahead.begin();
                        while(it_ahead != ahead.end() &&
                            *it_overlap == *it_ahead) {

                            ++it_overlap;
                            ++it_ahead;
                            ++len;
                        }
                    }

                    // test if factor is currently the longest
                    if(len > flen) {
                        flen = len;
                        fsrc = pos;
                    }
                }
                ++pos;
            }

            if(flen >= m_threshold) {
                // factor
                coder.encode_factor(lzss::Factor(
                    i, i-window.size()+fsrc, flen));
            } else {
                // unfactorized symbols
                auto it = ahead.begin();
                for(size_t k = 0; k < flen; k++) {
                    coder.encode_literal(*it++);
                }
            }

            // advance
            i += flen;

            size_t advance = flen;
            while(advance-- && !ahead.empty()) {
                // move
                window.push_back(ahead.pop_front());

                if(!ins.eof() && (c = ins.get()) >= 0) {
                    ahead.push_back(uliteral_t(c));
                }
            }
        }
    }

    inline virtual std::unique_ptr<Decompressor> decompressor() const override {
        return Algorithm::instance<LZSSDecompressor<lzss_coder_t>>();
    }
};

} //ns
