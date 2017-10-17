/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "FontLanguageListCache.h"
#include "MinikinInternal.h"
#include "HyphenatorMap.h"

#include <inttypes.h>

namespace minikin {

namespace {
constexpr SubtagBits LANGUAGE = SubtagBits::LANGUAGE;
constexpr SubtagBits SCRIPT = SubtagBits::SCRIPT;
constexpr SubtagBits REGION = SubtagBits::REGION;
constexpr SubtagBits VARIANT = SubtagBits::VARIANT;

constexpr int DEFAULT_MIN_PREFIX = 2;
constexpr int DEFAULT_MAX_PREFIX = 2;
}  // namespace


// Following two function's implementations are here since Hyphenator.cpp can't include
// HyphenatorMap.h due to harfbuzz dependency on the host binary.
void addHyphenator(const std::string& localeStr, const Hyphenator* hyphenator) {
    HyphenatorMap::add(localeStr, hyphenator);
}

void addHyphenatorAlias(const std::string& fromLocaleStr, const std::string& toLocaleStr) {
    HyphenatorMap::addAlias(fromLocaleStr, toLocaleStr);
}

HyphenatorMap::HyphenatorMap()
      : mSoftHyphenOnlyHyphenator(Hyphenator::loadBinary(
              nullptr, DEFAULT_MIN_PREFIX, DEFAULT_MAX_PREFIX, "")) {
}

void HyphenatorMap::addInternal(const std::string& localeStr, const Hyphenator* hyphenator) {
    const FontLanguage locale(localeStr);
    android::AutoMutex _l(gMinikinLock);
    addInternalLocked(locale, hyphenator);
}

void HyphenatorMap::addInternalLocked(const FontLanguage& locale, const Hyphenator* hyphenator) {
    // Add given locale first.
    // Overwrite even if there is already a fallback entry.
    mMap[locale.getIdentifier()] = hyphenator;
}

void HyphenatorMap::addAliasInternal(
        const std::string& fromLocaleStr, const std::string& toLocaleStr) {
    const FontLanguage fromLocale(fromLocaleStr);
    const FontLanguage toLocale(toLocaleStr);
    android::AutoMutex _l(gMinikinLock);
    auto it = mMap.find(toLocale.getIdentifier());
    MINIKIN_ASSERT(it != mMap.end(), "Target Hyphenator not found.");
    addInternalLocked(fromLocale, it->second);
}

const Hyphenator* HyphenatorMap::lookupInternal(const FontLanguage& locale) {
    const uint64_t id = locale.getIdentifier();
    android::AutoMutex _l(gMinikinLock);
    const Hyphenator* result = lookupByIdentifierLocked(id);
    if (result != nullptr) {
        return result;  // Found with exact match.
    }

    // First, try with dropping emoji extensions.
    result = lookupBySubtagLocked(locale, LANGUAGE | REGION | SCRIPT | VARIANT);
    if (result != nullptr) {
        goto insert_result_and_return;
    }
    // If not found, try with dropping script.
    result = lookupBySubtagLocked(locale, LANGUAGE | REGION | VARIANT);
    if (result != nullptr) {
        goto insert_result_and_return;
    }
    // If not found, try with dropping script and region code.
    result = lookupBySubtagLocked(locale, LANGUAGE | VARIANT);
    if (result != nullptr) {
        goto insert_result_and_return;
    }
    // If not found, try only with language code.
    result = lookupBySubtagLocked(locale, LANGUAGE);
    if (result != nullptr) {
        goto insert_result_and_return;
    }
    // Still not found, try only with script.
    result = lookupBySubtagLocked(locale, SCRIPT);
    if (result != nullptr) {
        goto insert_result_and_return;
    }

    // If not found, use soft hyphen only hyphenator.
    result = mSoftHyphenOnlyHyphenator;

insert_result_and_return:
    mMap.insert(std::make_pair(id, result));
    return result;
}

const Hyphenator* HyphenatorMap::lookupByIdentifierLocked(uint64_t id) const {
    auto it = mMap.find(id);
    return it == mMap.end() ? nullptr : it->second;
}

const Hyphenator* HyphenatorMap::lookupBySubtagLocked(const FontLanguage& locale,
        SubtagBits bits) const {
    const FontLanguage partialLocale = locale.getPartialLocale(bits);
    if (!partialLocale.isSupported() || partialLocale == locale) {
        return nullptr;  // Skip the partial locale result in the same locale or not supported.
    }
    return lookupByIdentifierLocked(partialLocale.getIdentifier());
}

}  // namespace minikin