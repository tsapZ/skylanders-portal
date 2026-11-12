#include "portal/prompt_tables.h"

namespace skylanders::portal {
namespace {

using B = PromptButton;

// Spyro's Adventure (dumps of the title, the hub and the pause menu's pages): 128x128 DXT5 each, upright. The game
// shows no X / Y / Back prompt on those screens.
constexpr PromptTexture kSpyrosAdventure[] = {
    {0xB9E607D0F791F494, B::kA, false},
    {0x6537E008ADF32F89, B::kB, false},
    {0x9D6716CD0FDB72B0, B::kStart, false},
};

// The 256x168 atlas Swap Force and SuperChargers share (the same texels): A B Y X across the top, LT RT between LB
// and RB, then a keyhole, a badge, an exclamation mark (not buttons: they stay) and LS RS.
constexpr AtlasCell kAtlas256[] = {
    {B::kA, 3, 7, 43, 43},     {B::kB, 51, 6, 42, 44},     {B::kY, 97, 7, 43, 43},     {B::kX, 144, 7, 43, 43},
    {B::kLT, 82, 58, 41, 53},  {B::kRT, 132, 58, 41, 53},  {B::kLB, 5, 71, 70, 31},    {B::kRB, 182, 71, 69, 31},
    {B::kLS, 144, 117, 45, 44}, {B::kRS, 201, 117, 45, 44},
};
// Imaginators' own 256x168: the same but for slightly smaller bumpers.
constexpr AtlasCell kAtlas256Imaginators[] = {
    {B::kA, 3, 7, 43, 43},     {B::kB, 51, 6, 42, 44},     {B::kY, 97, 7, 43, 43},     {B::kX, 144, 7, 43, 43},
    {B::kLT, 82, 58, 41, 53},  {B::kRT, 132, 58, 41, 53},  {B::kLB, 5, 71, 69, 30},    {B::kRB, 181, 71, 69, 30},
    {B::kLS, 144, 117, 45, 44}, {B::kRS, 201, 117, 45, 44},
};
// Swap Force's 256x83: A Y X LB LT, then RT RB LS RS B.
constexpr AtlasCell kSwapForce83[] = {
    {B::kA, 5, 1, 38, 39},     {B::kY, 47, 1, 39, 39},     {B::kX, 91, 1, 39, 39},    {B::kLB, 138, 8, 64, 28},
    {B::kLT, 208, 1, 38, 39},  {B::kRT, 4, 42, 38, 39},    {B::kRB, 47, 49, 63, 28},  {B::kLS, 117, 43, 38, 38},
    {B::kRS, 164, 43, 38, 38}, {B::kB, 206, 42, 39, 39},
};
// SuperChargers' 128x209: A B Y, X LB, LT RT, RB LS, RS and the d-pad.
constexpr AtlasCell kSuperChargers209[] = {
    {B::kA, 1, 2, 38, 38},     {B::kB, 43, 1, 39, 39},     {B::kY, 85, 1, 39, 39},    {B::kX, 1, 42, 39, 39},
    {B::kLB, 43, 49, 64, 28},  {B::kLT, 4, 83, 38, 39},    {B::kRT, 49, 83, 38, 39},  {B::kRB, 3, 131, 63, 28},
    {B::kLS, 73, 125, 38, 38}, {B::kRS, 5, 166, 38, 38},   {B::kDpad, 47, 164, 42, 39},
};
// Imaginators' 128x250: as SuperChargers', then two Back pills (one of them likely drawn mirrored for Start: left as
// they are) and the d-pad.
constexpr AtlasCell kImaginators250[] = {
    {B::kA, 1, 1, 38, 39},     {B::kB, 43, 1, 39, 39},     {B::kY, 85, 1, 39, 39},    {B::kX, 1, 42, 39, 39},
    {B::kLB, 43, 49, 64, 27},  {B::kLT, 4, 83, 38, 39},    {B::kRT, 49, 83, 38, 39},  {B::kRB, 3, 131, 64, 27},
    {B::kLS, 73, 125, 38, 38}, {B::kRS, 5, 166, 38, 38},   {B::kDpad, 2, 205, 42, 39},
};

constexpr PromptAtlas kSwapForceAtlases[] = {
    {0xCC26ED3F9A43B62C, 256, 168, kAtlas256},
    {0x56472FD0986C8E90, 256, 83, kSwapForce83},
};
constexpr PromptAtlas kSuperChargersAtlases[] = {
    {0xCC26ED3F9A43B62C, 256, 168, kAtlas256},
    {0x4EB150431A1F30AD, 128, 209, kSuperChargers209},
};
constexpr PromptAtlas kImaginatorsAtlases[] = {
    {0x920A9DE0B8A56A2B, 256, 168, kAtlas256Imaginators},
    {0x774468E270AC7289, 128, 250, kImaginators250},
};

}  // namespace

std::span<const PromptTexture> PromptTexturesOf(std::string_view game) {
  if (game == "Spyro's Adventure") return kSpyrosAdventure;
  return {};
}

std::span<const PromptAtlas> PromptAtlasesOf(std::string_view game) {
  if (game == "Swap Force") return kSwapForceAtlases;
  if (game == "SuperChargers") return kSuperChargersAtlases;
  if (game == "Imaginators") return kImaginatorsAtlases;
  return {};
}

}  // namespace skylanders::portal
