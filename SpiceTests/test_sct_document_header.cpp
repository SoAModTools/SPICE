#include "../SpiceSCT/SctDocument.h"

#include <gtest/gtest.h>

#include <type_traits>

static_assert(std::is_default_constructible_v<spice::sct::SctDocument>);
static_assert(std::is_same_v<decltype(spice::sct::SctDocumentFooterEntry{}.kind),
    spice::sct::SctDocumentFooterEntryKind>);

TEST(SctDocumentHeader, IsSelfContainedAndUsesDocumentOwnedFooterKinds) {
    spice::sct::SctDocument document;
    const auto footerId = document.allocateFooterEntryId();
    document.footerEntries.push_back({footerId, spice::sct::SctDocumentFooterEntryKind::SctString,
        spice::sct::SctEditableText{"text"}});
    ASSERT_EQ(document.footerEntries.size(), 1u);
    EXPECT_EQ(document.footerEntries[0].kind, spice::sct::SctDocumentFooterEntryKind::SctString);
}
