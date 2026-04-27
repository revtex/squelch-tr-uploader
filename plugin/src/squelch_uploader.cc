// SPDX-License-Identifier: GPL-3.0-or-later
//
// squelch_uploader.cc — Trunk-Recorder Plugin_Api subclass for Squelch.
//
// Phase C-2 wires up lifecycle hooks: parse_config(), init(), start(), stop()
// and the per-call/unit hooks. call_end() currently logs a placeholder; the
// actual multipart upload lands in Phase C-3.
//
// The factory is exported with BOOST_DLL_ALIAS so TR's plugin loader picks it
// up identically to the upstream rdioscanner_uploader.

#include "squelch_uploader.h"
#include "squelch_uploader/config.h"

#include "trunk-recorder/plugin_manager/plugin_api.h"

#include <boost/dll/alias.hpp>
#include <boost/log/trivial.hpp>
#include <boost/shared_ptr.hpp>

#include <string>
#include <vector>

namespace squelch
{

    namespace
    {

        // Mask all but the leading 6 chars of an API key. Never log the full secret.
        std::string redact(const std::string &key)
        {
            if (key.size() <= 6)
                return std::string(key.size(), '*');
            return key.substr(0, 6) + "…";
        }

    } // namespace

    class SquelchUploader : public Plugin_Api
    {
    public:
        SquelchUploader() = default;
        ~SquelchUploader() override = default;

        int parse_config(json config_data) override
        {
            std::string error;
            auto parsed = ::squelch::Config::parse(config_data, &error);
            if (!parsed)
            {
                BOOST_LOG_TRIVIAL(error)
                    << "[" << kPluginName << "] invalid config: " << error;
                return 1;
            }
            config_ = std::move(*parsed);
            BOOST_LOG_TRIVIAL(info)
                << "[" << kPluginName << "] config parsed: server="
                << config_.server << " apiKey=" << redact(config_.api_key)
                << " shortName='" << config_.short_name << "'"
                << " systemId="
                << (config_.system_id ? std::to_string(*config_.system_id)
                                      : std::string("<none>"));
            return 0;
        }

        // NOTE: ::Config is TR's host-config struct; squelch::Config is ours.
        int init(::Config *tr_config,
                 std::vector<Source *> sources,
                 std::vector<System *> systems) override
        {
            if (tr_config != nullptr)
            {
                frequency_format = tr_config->frequency_format;
            }
            (void)sources;
            (void)systems;
            BOOST_LOG_TRIVIAL(info)
                << "[" << kPluginName << " " << kPluginVersion << "] init";
            return 0;
        }

        int start() override
        {
            BOOST_LOG_TRIVIAL(info) << "[" << kPluginName << "] start";
            return 0;
        }

        int stop() override
        {
            BOOST_LOG_TRIVIAL(info) << "[" << kPluginName << "] stop";
            return 0;
        }

        // ----- per-call hooks --------------------------------------------------

        int call_start(Call *call) override
        {
            // TODO(phase-C-3+): track in-flight calls if we end up needing
            // start/end correlation for multi-recorder rigs.
            (void)call;
            return 0;
        }

        int call_end(Call_Data_t call_info) override
        {
            // TODO(phase-C-3): build multipart body from call_info and POST to
            // <server>/api/v1/calls. For now just record what would have shipped.
            BOOST_LOG_TRIVIAL(info)
                << "[" << kPluginName << "] would upload " << call_info.filename;
            return 0;
        }

        // ----- recorder / system / source setup --------------------------------

        int setup_recorder(Recorder *recorder) override
        {
            // TODO(phase-C-3+): bind per-recorder state if needed.
            (void)recorder;
            return 0;
        }

        int setup_system(System *system) override
        {
            // TODO(phase-C-3+): map TR system -> Squelch systemId here.
            (void)system;
            return 0;
        }

        int setup_systems(std::vector<System *> systems) override
        {
            // TODO(phase-C-3+): validate that each declared system has a Squelch
            // counterpart in our config.
            (void)systems;
            return 0;
        }

        int setup_sources(std::vector<Source *> sources) override
        {
            // TODO(phase-C-3+).
            (void)sources;
            return 0;
        }

        // ----- unit-level hooks ------------------------------------------------

        int unit_registration(System *sys, long source_id) override
        {
            // TODO(phase-C-3+): forward unit-registration events when Squelch
            // gains a unit-registration endpoint.
            (void)sys;
            (void)source_id;
            return 0;
        }

        int unit_deregistration(System *sys, long source_id) override
        {
            (void)sys;
            (void)source_id;
            return 0;
        }

        int unit_acknowledge_response(System *sys, long source_id) override
        {
            (void)sys;
            (void)source_id;
            return 0;
        }

        int unit_data_grant(System *sys, long source_id) override
        {
            (void)sys;
            (void)source_id;
            return 0;
        }

        int unit_answer_request(System *sys,
                                long source_id,
                                long talkgroup) override
        {
            (void)sys;
            (void)source_id;
            (void)talkgroup;
            return 0;
        }

        int unit_location(System *sys,
                          long source_id,
                          long talkgroup_num) override
        {
            (void)sys;
            (void)source_id;
            (void)talkgroup_num;
            return 0;
        }

        // ----- factory ---------------------------------------------------------

        static boost::shared_ptr<SquelchUploader> create()
        {
            return boost::shared_ptr<SquelchUploader>(new SquelchUploader());
        }

        // Test-only accessor (not part of the plugin ABI).
        const ::squelch::Config &config() const { return config_; }

    private:
        ::squelch::Config config_{};
    };

} // namespace squelch

// Exported with the same alias name (`create_plugin`) as upstream
// rdioscanner_uploader so TR's loader picks it up identically.
BOOST_DLL_ALIAS(
    squelch::SquelchUploader::create, // <-- this function is exported with…
    create_plugin                     // <-- …this alias name
)
