#pragma once

#include <tudocomp/util.hpp>

#include <tudocomp/Compressor.hpp>
#include <tudocomp/Tags.hpp>

#include <tudocomp/compressors/lzss/FactorBuffer.hpp>
#include <tudocomp/compressors/lzss/FactorizationStats.hpp>
#include <tudocomp/compressors/lzss/UnreplacedLiterals.hpp>

#include <tudocomp/compressors/lcpcomp/lcpcomp.hpp>

#include <tudocomp/ds/DSManager.hpp>
#include <tudocomp/ds/providers/DivSufSort.hpp>
#include <tudocomp/ds/providers/ISAFromSA.hpp>
#include <tudocomp/ds/providers/PhiAlgorithm.hpp>
#include <tudocomp/ds/providers/PhiFromSA.hpp>
#include <tudocomp/ds/providers/LCPFromPLCP.hpp>

#include <tudocomp_stat/StatPhase.hpp>

#include <tudocomp/compressors/lcpcomp/compress/ArraysComp.hpp>
#include <tudocomp/decompressors/LCPDecompressor.hpp>

#include <tudocomp/compressors/lzss/LZSSCoder.hpp>

namespace tdc {

/// Factorizes the input by finding redundant phrases in a re-ordered version
/// of the LCP table.
template<
    typename lzss_coder_t,
    typename strategy_t = lcpcomp::ArraysComp,
    typename ds_t = DSManager<DivSufSort, PhiFromSA, PhiAlgorithm, LCPFromPLCP, ISAFromSA>>
class LCPCompressor : public Compressor {
public:
    inline static Meta meta() {
        Meta m(Compressor::type_desc(), "lcpcomp",
            "Computes the lcpcomp factorization of the input.");
        m.param("coder", "The output encoder.")
            .strategy<lzss_coder_t>(lzss_bidirectional_coder_type());
        m.param("comp", "The factorization strategy for compression.")
            .strategy<strategy_t>(lcpcomp::comp_strategy_type(),
                Meta::Default<lcpcomp::ArraysComp>());
        m.param("ds", "The text data structure provider.")
            .strategy<ds_t>(ds::type(), Meta::Default<DSManager<DivSufSort, PhiFromSA, PhiAlgorithm, LCPFromPLCP, ISAFromSA>>());
        m.param("threshold", "The minimum factor length.").primitive(5);
        m.param("flatten", "Flatten reference chains after factorization.")
            .primitive(1); // 0 or 1
        m.inherit_tag<ds_t>(tags::require_sentinel);
        m.inherit_tag<lzss_coder_t>(tags::lossy);
        return m;
    }

    using Compressor::Compressor;

    inline virtual void compress(Input& input, Output& output) override {
        auto in = input.as_view();

        ds_t text(config().sub_config("ds"), in);
        StatPhase::wrap("Construct Text DS", [&]{
            strategy_t::construct_textds(text);
        });

        // read options
        const len_t threshold = config().param("threshold").as_uint();
        lzss::FactorBuffer<> factors;

        StatPhase::wrap("Factorize", [&]{
            // Factorize
            strategy_t strategy(config().sub_config("comp"));
            strategy.factorize(text, threshold, factors);
        });

        // sort factors
        StatPhase::wrap("Sort Factors", [&]{ factors.sort(); });

        if(config().param("flatten").as_bool()) {
            // flatten factors
            StatPhase::wrap("Flatten Factors", [&]{ factors.flatten(); });
        }

        // statistics
        IF_STATS({
            lzss::FactorizationStats stats(factors, in.size());
            stats.log();
        })

        // encode
        StatPhase::wrap("Encode Factors", [&]{
            auto coder = lzss_coder_t(config().sub_config("coder")).encoder(
                output,
                lzss::UnreplacedLiterals<decltype(in), decltype(factors)>(in, factors));

            coder.encode_text(in, factors);
        });
    }

    inline virtual std::unique_ptr<Decompressor> decompressor() const override {
        return Algorithm::instance<LCPDecompressor<lzss_coder_t>>();
    }
};

/// \brief Contains factorization and decoding strategies for
///        the  \ref LCPCompressor.
namespace lcpcomp {
    // only here for Doxygen
}

}

