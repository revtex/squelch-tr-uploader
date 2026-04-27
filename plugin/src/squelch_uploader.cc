// SPDX-License-Identifier: GPL-3.0-or-later
//
// squelch_uploader.cc — Trunk-Recorder Plugin_Api subclass for Squelch.
//
// On call_end() the plugin builds an UploadRequest from the Call_Data_t,
// runs the 50 MiB pre-flight, and hands the job to the Uploader, which
// runs a single background thread that POSTs multipart/form-data to
// `<server>/api/v1/calls` with `Authorization: Bearer <api-key>`.
//
// The factory is exported with BOOST_DLL_ALIAS under the alias name
// `create_plugin`, which is what TR's plugin loader looks for.

#include "squelch_uploader.h"
#include "squelch_uploader/config.h"
#include "squelch_uploader/upload_request.h"
#include "squelch_uploader/uploader.h"

#include "trunk-recorder/plugin_manager/plugin_api.h"

#include <boost/dll/alias.hpp>
#include <boost/log/trivial.hpp>
#include <boost/shared_ptr.hpp>

#include <cstdint>
#include <memory>
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

            if (config_.server.empty() || config_.api_key.empty())
            {
                // parse_config should have caught this, but defend anyway.
                BOOST_LOG_TRIVIAL(error)
                    << "[" << kPluginName
                    << "] start without server/apiKey; uploads disabled";
                return 1;
            }

            uploader_ = std::make_unique<Uploader>(
                build_upload_url(config_.server),
                config_.api_key,
                config_.max_retries);
            uploader_->start();
            BOOST_LOG_TRIVIAL(info)
                << "[" << kPluginName << "] uploader started maxRetries="
                << config_.max_retries;
            return 0;
        }

        int stop() override
        {
            BOOST_LOG_TRIVIAL(info) << "[" << kPluginName << "] stop";
            if (uploader_)
            {
                uploader_->stop();
                uploader_.reset();
                BOOST_LOG_TRIVIAL(info)
                    << "[" << kPluginName << "] uploader stopped";
            }
            return 0;
        }

        // ----- per-call hooks --------------------------------------------------

        int call_start(Call *call) override
        {
            (void)call;
            return 0;
        }

        int call_end(Call_Data_t call_info) override
        {
            if (!config_.system_id)
            {
                BOOST_LOG_TRIVIAL(warning)
                    << "[" << kPluginName
                    << "] no systemId configured; skipping upload of "
                    << call_info.filename;
                return 0;
            }

            if (!uploader_)
            {
                BOOST_LOG_TRIVIAL(error)
                    << "[" << kPluginName
                    << "] uploader not running; dropping upload of "
                    << call_info.filename;
                return 1;
            }

            // Pick the on-disk audio path the way TR exposes it: use the
            // compressed file when call_info.compress_wav is set, otherwise
            // the raw `filename`.
            CallData call;
            call.talkgroup = call_info.talkgroup;
            call.start_time = call_info.start_time;
            call.freq = call_info.freq;
            call.length = call_info.length;
            call.error_count = call_info.error_count;
            call.spike_count = call_info.spike_count;
            call.talkgroup_tag = call_info.talkgroup_tag;
            call.talkgroup_alpha_tag = call_info.talkgroup_alpha_tag;
            call.talkgroup_description = call_info.talkgroup_description;
            call.talkgroup_group = call_info.talkgroup_group;
            call.audio_path =
                call_info.compress_wav ? call_info.converted : call_info.filename;
            call.short_name = call_info.short_name;
            call.patched_talkgroups = std::vector<unsigned long>(
                call_info.patched_talkgroups.begin(),
                call_info.patched_talkgroups.end());

            call.transmission_source_list.reserve(
                call_info.transmission_source_list.size());
            for (const auto &s : call_info.transmission_source_list)
            {
                CallSourceLite lite;
                lite.source = s.source;
                lite.time = s.time;
                lite.position = s.position;
                lite.length = 0.0; // TR's Call_Source has no per-tx length;
                                   // duration_ms covers the whole call.
                lite.emergency = s.emergency;
                lite.signal_system = s.signal_system;
                lite.tag = s.tag;
                call.transmission_source_list.push_back(std::move(lite));
            }

            call.transmission_error_list.reserve(
                call_info.transmission_error_list.size());
            for (const auto &f : call_info.transmission_error_list)
            {
                CallFreqLite lite;
                lite.freq = call_info.freq; // per-call freq; TR's Call_Error
                                            // doesn't carry one.
                lite.time = f.time;
                lite.position = f.position;
                lite.total_len = f.total_len;
                lite.error_count = static_cast<long>(f.error_count);
                lite.spike_count = static_cast<long>(f.spike_count);
                call.transmission_error_list.push_back(std::move(lite));
            }

            // Pre-flight: file must exist and be ≤ 50 MiB.
            std::uintmax_t audio_size = 0;
            std::string preflight_error;
            if (!check_audio_file(call.audio_path, &audio_size,
                                  &preflight_error))
            {
                BOOST_LOG_TRIVIAL(error)
                    << "[" << kPluginName << "] " << preflight_error;
                return 1;
            }

            UploadRequest req =
                UploadRequest::from_call_data(call, *config_.system_id);

            UploadJob job;
            job.request = std::move(req);
            job.debug_tag = "tg=" + std::to_string(call.talkgroup) +
                            " started=" + job.request.started_at +
                            " bytes=" + std::to_string(audio_size);

            uploader_->enqueue(std::move(job));
            return 0;
        }

        // ----- recorder / system / source setup --------------------------------

        int setup_recorder(Recorder *recorder) override
        {
            (void)recorder;
            return 0;
        }

        int setup_system(System *system) override
        {
            (void)system;
            return 0;
        }

        int setup_systems(std::vector<System *> systems) override
        {
            (void)systems;
            return 0;
        }

        int setup_sources(std::vector<Source *> sources) override
        {
            (void)sources;
            return 0;
        }

        // ----- unit-level hooks ------------------------------------------------

        int unit_registration(System *sys, long source_id) override
        {
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

    private:
        ::squelch::Config config_{};
        std::unique_ptr<Uploader> uploader_;
    };

} // namespace squelch

// Exported under the alias name `create_plugin`, which is what TR's plugin
// loader looks for.
BOOST_DLL_ALIAS(
    squelch::SquelchUploader::create, // <-- this function is exported with…
    create_plugin                     // <-- …this alias name
)
