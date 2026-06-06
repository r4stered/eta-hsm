// Standalone diagram tool for the cd_player example: include the machine's
// header and print its diagram, walking the same `constexpr` table the machine
// runs on so the diagram can never drift from the machine.
//
//   cd_player_diagram            # PlantUML (default)
//   cd_player_diagram --mermaid  # Mermaid stateDiagram-v2
//
// Any machine gets a diagram the same way: include its header and call
// to_plantuml<table>() / to_mermaid<table>() from a main like this one.

#include <cstring>
#include <iostream>

#include "eta_hsm/diagram/mermaid.hpp"
#include "eta_hsm/diagram/plantuml.hpp"
#include "eta_hsm/examples/cd_player/cd_player.hpp"

int main(int argc, char** argv)
{
    using eta_hsm::examples::cd_player::player;

    bool const mermaid = argc > 1 && std::strcmp(argv[1], "--mermaid") == 0;
    std::cout << (mermaid ? eta_hsm::diagram::to_mermaid<player>() : eta_hsm::diagram::to_plantuml<player>());
    return 0;
}
