#include "../SpiceTrade/SpiceTrade.h"

#include <gtest/gtest.h>

namespace {
using namespace spice::trade::alx;

TEST(SpiceTradeHardCut, WhitelistAndStableIdentitiesAreCanonical)
{
    ASSERT_EQ(whitelistedTables().size(), 15U);
    EXPECT_TRUE(whitelistedTableKind("ENEMYTASK.CSV").has_value());
    EXPECT_EQ(canonicalIdentity(EnemyEntryId{42}), "enemy.42");
    EXPECT_EQ(canonicalIdentity(EnemyTaskEntryId{EnemyEntryId{42}, 3}), "enemytask.42.3");
    EXPECT_EQ(canonicalIdentity(EnemyEncounterEntryId{"MA000.ENP", 7}), "enemyencounter.ma000.enp.7");
}

} // namespace
