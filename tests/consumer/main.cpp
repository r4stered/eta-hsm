// Compiles only if find_package(EtaHsm) exposed the header include path and
// propagated C++26 + -freflection from the imported INTERFACE target.
#include "eta_hsm/reflect/reflection_smoke.hpp"

#include <cstdio>

enum class Signal { Red, Yellow, Green };

int main() {
  static_assert(eta_hsm::smoke::enumerator_count<Signal>() == 3);
  std::printf("eta_hsm consumer OK: Signal has %zu enumerators, first is %.*s\n",
              eta_hsm::smoke::enumerator_count<Signal>(),
              static_cast<int>(eta_hsm::smoke::enumerator_name<Signal>(0).size()),
              eta_hsm::smoke::enumerator_name<Signal>(0).data());
  return 0;
}
