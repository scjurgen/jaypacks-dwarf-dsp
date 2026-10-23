/* Included before every Lua source file (CMakeLists.txt, -include), so the Lua sources stay as released.
 *
 * Lua seeds its string hash from the time and an address, which changes the order of pairs() between
 * runs. A fixed seed makes a script's behaviour reproducible on the Mac and on the Dwarf. The seed does not
 * protect against hash flooding; the scripts are limited by memory and instruction budgets instead. */
#define luai_makeseed(L) 0u
