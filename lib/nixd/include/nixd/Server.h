#pragma once

#include "nixd/EvalDraftStore.h"

#include "lspserver/Connection.h"
#include "lspserver/DraftStore.h"
#include "lspserver/Function.h"
#include "lspserver/LSPServer.h"
#include "lspserver/Logger.h"
#include "lspserver/Path.h"
#include "lspserver/Protocol.h"
#include "lspserver/SourceCode.h"

#include <llvm/ADT/FunctionExtras.h>
#include <llvm/Support/JSON.h>

namespace nixd {

namespace configuration {

struct InstallableConfigurationItem {
  std::vector<std::string> args;
  std::string installable;
};

struct TopLevel {
  /// Nix installables that will be used for root translation unit.
  std::optional<InstallableConfigurationItem> installable;

  /// Get installable arguments specified in this config, fallback to file \p
  /// Fallback if 'installable' is not set.
  [[nodiscard]] std::tuple<nix::Strings, std::string>
  getInstallable(std::string Fallback) const;
};

bool fromJSON(const llvm::json::Value &Params, TopLevel &R, llvm::json::Path P);
bool fromJSON(const llvm::json::Value &Params, InstallableConfigurationItem &R,
              llvm::json::Path P);
} // namespace configuration

/// The server instance, nix-related language features goes here
class Server : public lspserver::LSPServer {

  EvalDraftStore DraftMgr;
  boost::asio::thread_pool Pool = boost::asio::thread_pool(1);

  lspserver::ClientCapabilities ClientCaps;

  configuration::TopLevel Config;

  std::shared_ptr<const std::string> getDraft(lspserver::PathRef File) const;

  std::mutex ResultLock;
  std::shared_ptr<EvalResult> LastValidResult; // GUARDED_BY(ResultLock)
  std::shared_ptr<EvalResult> CachedResult;    // GUARDED_BY(ResultLock)

  void addDocument(lspserver::PathRef File, llvm::StringRef Contents,
                   llvm::StringRef Version);

  void removeDocument(lspserver::PathRef File) { DraftMgr.removeDraft(File); }

  /// LSP defines file versions as numbers that increase.
  /// treats them as opaque and therefore uses strings instead.
  static std::string encodeVersion(std::optional<int64_t> LSPVersion);

  llvm::unique_function<void(const lspserver::PublishDiagnosticsParams &)>
      PublishDiagnostic;

  llvm::unique_function<void(const lspserver::ConfigurationParams &,
                             lspserver::Callback<configuration::TopLevel>)>
      WorkspaceConfiguration;

  void withEval(
      std::string Fallback,
      llvm::unique_function<void(std::shared_ptr<EvalResult> Result)> Then);

public:
  Server(std::unique_ptr<lspserver::InboundPort> In,
         std::unique_ptr<lspserver::OutboundPort> Out);

  ~Server() override { Pool.join(); }

  void fetchConfig();

  void clearDiagnostic(lspserver::PathRef Path);

  void clearDiagnostic(const lspserver::URIForFile &FileUri);

  void diagNixError(lspserver::PathRef Path, const nix::BaseError &Err,
                    std::optional<int64_t> Version);

  void invalidateEvalCache();

  void onInitialize(const lspserver::InitializeParams &,
                    lspserver::Callback<llvm::json::Value>);

  void onInitialized(const lspserver::InitializedParams &Params) {
    fetchConfig();
  }

  void onDocumentDidOpen(const lspserver::DidOpenTextDocumentParams &Params);

  void
  onDocumentDidChange(const lspserver::DidChangeTextDocumentParams &Params);

  void publishStandaloneDiagnostic(lspserver::URIForFile Uri,
                                   std::string Content,
                                   std::optional<int64_t> LSPVersion);

  void onWorkspaceDidChangeConfiguration(
      const lspserver::DidChangeConfigurationParams &) {
    fetchConfig();
  }
  void onHover(const lspserver::TextDocumentPositionParams &,
               lspserver::Callback<llvm::json::Value>);
};

}; // namespace nixd
