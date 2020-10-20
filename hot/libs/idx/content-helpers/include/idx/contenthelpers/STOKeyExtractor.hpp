//
// Created by ron on 02/10/2020.
//

#ifndef STO_STOKEYEXTRACTOR_H
#define STO_STOKEYEXTRACTOR_H

namespace idx { namespace contenthelpers {

/**
 * For pair like types the PairKeyExtractor returns the key part of the pair
 *
 * @tparam PairLikeType a pair like type having at least 'first' and 'second' field with the 'first' field pointing to the key part and the 'second' field pointing to the value part
 */
        template<typename STOEelemnt>
        struct STOKeyExtractor {
            using KeyType = decltype(std::declval<STOEelemnt>()->key);

            inline KeyType operator()(STOEelemnt const &value) const {
                return value->key;
            }
        };

    } }

#endif //STO_STOKEYEXTRACTOR_H
