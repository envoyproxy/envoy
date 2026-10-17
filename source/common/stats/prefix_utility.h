#pragma once

#include <cstdint>
#include <optional>

#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

#include "source/common/stats/utility.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Merges a parent prefix with an owner's own name and tags to produce a TaggedStatName. This helper
 * extracts the well-known tag from the parent prefix and merges it with the input tags to produce
 * the final TaggedStatName.
 *
 * The helper assumes the caller has already handled the dots correctly, so it can concatenate the
 * parent prefix and the own name directly.
 *
 * The merged tagged name is `<prefix><name>` and the merged base name is
 * `<parent_root><base_name>`, where `<parent_root>` is `<prefix>` with its well-known tag value
 * removed (e.g. "http.ingress." -> "http."), or the full `<prefix>` when it carries no well-known
 * tag. When neither the parent prefix nor `tags` contributes a tag, the base and tagged names are
 * identical.
 *
 * @param symbol_table the symbol table used to pre-encode the names and tags.
 * @param prefix the parent prefix, whose well-known variable segment is extracted as a tag when
 *        the prefix ends with a trailing '.' (e.g. "http.<hcm>." / "cluster.<name>."); may be
 *        empty.
 * @param base_name the tag-extracted own name (every own tag value removed).
 * @param tags one {tag_name, value} per variable segment of the own name. When empty,
 *        `name` is ignored and `base_name` is used for both forms.
 * @param name the tagged own name (own tag values interleaved). Combined with `prefix` this must
 *        equal the legacy flat prefix. Ignored when `tags` is empty.
 */
TaggedStatName mergeStatPrefix(SymbolTable& symbol_table, absl::string_view prefix,
                               absl::string_view base_name, TagStringViewSpan tags = {},
                               absl::string_view name = {});

/**
 * Creates the prefix of the stats of a thread. A worker thread contributes the envoy.worker_id
 * tag and its tag-extracted prefix keeps a literal 'worker.' segment in place of the thread name
 * ('<prefix>worker.<suffix>'); any other thread name stays a plain, untagged segment.
 *
 * @param symbol_table the symbol table used to pre-encode the names and tags.
 * @param prefix empty or '.' suffixed parent prefix, e.g. "listener_manager.".
 * @param worker_id the thread's worker id (Event::Dispatcher::workerId()); std::nullopt when the
 *        thread is not a worker.
 * @param thread_name the thread (dispatcher) name, e.g. "worker_0" or "main_thread"; may be empty
 *        only when worker_id is empty.
 * @param suffix empty or '.' suffixed suffix, e.g. "dispatcher.".
 */
TaggedStatName workerStatPrefix(SymbolTable& symbol_table, absl::string_view prefix,
                                std::optional<uint32_t> worker_id, absl::string_view thread_name,
                                absl::string_view suffix = {});

} // namespace Stats
} // namespace Envoy
