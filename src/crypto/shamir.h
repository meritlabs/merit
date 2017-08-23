#ifndef SHAMIR_H

#include "crypto/finite_field.h"
#include "crypto/legendre.h"
#include "key.h"
#include <algorithm>
#include <vector>

namespace shamir
{
std::vector<legendre::FieldPoint> ShardElement(const finite_field::Element&);
std::vector<std::pair<CPrivKey, CPrivKey>> ShardKey(const CPrivKey&);
CPrivKey RecoverKey(const std::vector<std::pair<CPrivKey, CPrivKey>>&);
} // namespace shamir

#define SHAMIR_H
#endif
