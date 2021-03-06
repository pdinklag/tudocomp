#pragma once

#include <climits>
#include <cstdint>
#include <iostream>

#include <tudocomp/util.hpp>
#include <tudocomp/util/int_coder.hpp>
#include <tudocomp/io/Output.hpp>

namespace tdc {
namespace io {

/// \brief Wrapper for output streams that provides bitwise writing
/// functionality.
///
/// Bits are written into a buffer byte, which is written to the output when
/// it is either filled or when a flush is explicitly requested.
class BitOStream {
    OutputStream m_stream;

    uint8_t m_next;
    int8_t m_cursor;
    static constexpr int8_t MSB = 7;

    size_t m_bits_written = 0;

    inline bool is_dirty() const {
        return m_cursor != MSB;
    }

    inline void reset() {
        m_next = 0;
        m_cursor = MSB;
    }

    inline void write_next() {
        m_stream.put(char(m_next));
        reset();
    }

    struct BitSink {
        BitOStream* m_ptr;

        inline void write_bit(bool set) {
            m_ptr->write_bit(set);
        }

        template<typename T>
        inline void write_int(T value, size_t bits = sizeof(T) * CHAR_BIT) {
            m_ptr->write_int(value, bits);
        }
    };

    inline BitSink bit_sink() {
        return BitSink {
            this
        };
    }
public:
    /// \brief Constructs a bitwise output stream.
    ///
    /// \param output The underlying output stream.
    inline BitOStream(OutputStream&& output) : m_stream(std::move(output)) {
        reset();
    }

    /// \brief Constructs a bitwise output stream.
    ///
    /// \param output The underlying output.
    inline BitOStream(Output& output) : BitOStream(output.as_stream()) {
    }

    BitOStream(BitOStream&& other) = default;

    inline ~BitOStream() {
        uint8_t set_bits = MSB - m_cursor; // will only be in range 0 to 7
        if(m_cursor >= 2) {
            // if there are at least 3 bits free in the byte buffer,
            // write them into the cursor at the last 3 bit positions
            m_next |= set_bits;
            write_next();
        } else {
            // else write out the byte, and write the length into the
            // last 3 bit positions of the next byte
            write_next();
            m_next = set_bits;
            write_next();
        }
    }

    /// \brief Asserts that the next write operation starts on a byte boundary
    inline void assert_byte_boundary() const {
        CHECK(!is_dirty());
    }

    /// \brief Returns the underlying stream.
    ///
    /// Note that the stream does not include bits that have not yet been
    /// flushed.
    inline std::ostream& stream() {
        return m_stream;
    }

    /// \brief Writes a single bit to the output.
    /// \param set The bit value (0 or 1).
    inline void write_bit(bool set) {
        if (set) {
            m_next |= (1 << m_cursor);
        }

        m_cursor--;

        if (m_cursor < 0) {
            write_next();
        }

        ++m_bits_written;
    }

    /// Writes the bit representation of an integer in MSB first order to
    /// the output.
    ///
    /// \tparam The type of integer to write.
    /// \param value The integer to write.
    /// \param bits The amount of low bits of the value to write. By default,
    ///             this equals the bit width of type \c T.
    template<class T>
    inline void write_int(const T value, size_t bits = sizeof(T) * CHAR_BIT) {
        DCHECK_LE(bits, 64ULL);
        DCHECK_GE(m_cursor, 0);
        const size_t bits_left_in_next = size_t(m_cursor + 1);
        DCHECK_LE(bits_left_in_next, 8ULL);

        if(bits < bits_left_in_next) {
            // we are writing only few bits
            // simply use the bit-by-bit method
            ::tdc::write_int<T>(bit_sink(), value, bits);
        } else {
            // we are at least finishing the next byte
            const size_t in_bits = bits;

            // mask low bits of value
            size_t v = (bits < 64ULL) ?
                (size_t(value) & ((1ULL << bits) - 1ULL)) : size_t(value);

            // fill it up next byte and continue with remaining bits
            bits -= bits_left_in_next;
            m_next |= (v >> bits);
            write_next();

            v &= (1ULL << bits) - 1ULL; // mask remaining bits

            // write as many full bytes as possible
            if(bits >= 8ULL) {
                const size_t n = bits / 8ULL;
                bits %= 8ULL;

                // convert full bytes into BIG ENDIAN (!) representation
                #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                    const size_t v_bytes = __builtin_bswap64(v >> bits);
                #else
                    const size_t v_bytes = v >> bits;
                #endif

                const size_t off  = sizeof(size_t) - n;
                m_stream.write(((const char*)&v_bytes) + off, n);

                v &= (1ULL << bits) - 1ULL; // mask remaining bits
            }

            if(bits) {
                // write remaining bits
                // they (must) fit into the next byte, so just stuff them in
                DCHECK_LT(bits, 8ULL);
                DCHECK_LT(v, 256ULL);
                m_next = (v << (8ULL - bits));
                m_cursor = MSB - int8_t(bits);
                DCHECK_GE(m_cursor, 0);
            }

            m_bits_written += in_bits;
        }
    }

    // ########################################################
    // Only higher level functions that use bit_sink() below:
    // NB: Try to add new functions in IOUtil.hpp instead of here
    // ########################################################

    template<typename value_t>
    inline void write_unary(value_t v) {
        ::tdc::write_unary<value_t>(bit_sink(), v);
    }

    template<typename value_t>
    inline void write_ternary(value_t v) {
        ::tdc::write_ternary<value_t>(bit_sink(), v);
    }

    template<typename value_t>
    inline void write_elias_gamma(value_t v) {
        ::tdc::write_elias_gamma<value_t>(bit_sink(), v);
    }

    template<typename value_t>
    inline void write_elias_delta(value_t v) {
        ::tdc::write_elias_delta<value_t>(bit_sink(), v);
    }

    template<typename value_t>
    inline void write_rice(value_t v, uint8_t p) {
        ::tdc::write_rice<value_t>(bit_sink(), v, p);
    }

    /// \brief Writes a compressed integer to the input.
    ///
    /// The \e compressed form of an integer \c n is achieved by splitting
    /// up the bit representation of \c n in blocks of width \c b. For each
    /// non-zero block (in little endian order), a bit flag and the block itself
    /// is written.
    ///
    /// The flag is set to 1 if there is another block after the current one,
    /// and otherwise set to 0 to mark the block as the last one.
    ///
    /// \tparam The integer type to write.
    /// \param v The integer to write.
    /// \param b The block width in bits. The default is 7 bits.
    template<typename T>
    inline void write_compressed_int(T v, size_t b = 7) {
        ::tdc::write_compressed_int<T>(bit_sink(), v, b);
    }

    inline size_t bits_written() const { return m_bits_written; }
};

}}

