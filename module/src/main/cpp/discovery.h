#pragma once
namespace dumper {
// Returns an xDL handle owned by the caller. No candidate library is force-loaded.
void* discover_il2cpp(bool include_fallbacks, bool use_loader = true);
} // namespace dumper
