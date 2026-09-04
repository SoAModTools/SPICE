#include "../SpiceSCT/SctDocument.h"

#include <gtest/gtest.h>

#include <type_traits>

static_assert(std::is_default_constructible_v<spice::sct::SctDocument>);
static_assert(std::is_same_v<decltype(spice::sct::SctDocumentSupplementaryText{}.kind),
    spice::sct::SctSupplementaryTextKind>);

TEST(SctDocumentHeader, IsSelfContainedAndUsesDocumentOwnedSupplementaryTextKinds) {
    spice::sct::SctDocument document;
    const auto textId = document.allocateSupplementaryTextId();
    document.supplementaryText.push_back({textId, spice::sct::SctSupplementaryTextKind::SctString,
        spice::sct::SctMessage{std::nullopt,
            spice::sct::SctFormattedText{{spice::sct::SctTextChunk{"text"}}}}});
    ASSERT_EQ(document.supplementaryText.size(), 1u);
    EXPECT_EQ(document.supplementaryText[0].kind, spice::sct::SctSupplementaryTextKind::SctString);
}
