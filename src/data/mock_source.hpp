// mock_source.hpp — the design prototype's fixed 22-package dataset, used for
// `--mock` so the look can be exercised offline and matched against the handoff.
#pragma once

#include <string>
#include <vector>

#include "data/package_source.hpp"
#include "model/package.hpp"

namespace pacseek::data {

class MockSource : public PackageSource {
 public:
  std::vector<model::Package> LoadPackages() override;
  bool IsReadOnly() const override { return true; }
  std::string Name() const override { return "mock"; }
};

}  // namespace pacseek::data
