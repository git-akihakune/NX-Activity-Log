#include "Application.ExportJob.hpp"
#include <filesystem>
#include <fstream>
#include <system_error>
#include "nlohmann/json.hpp"
#include "utils/Utils.hpp"

namespace {
    void writeIndent(std::ostream & os, int level) {
        for (int i = 0; i < level; i++) {
            os << "    ";
        }
    }

    void writeJsonString(std::ostream & os, const std::string & str) {
        os << nlohmann::json(str).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
}

namespace Main {
    Application::ExportJob::ExportJob(Application * app, std::atomic<double> & percent) : Aether::ThreadPool::Job(), percent(percent) {
        this->app = app;
    }

    void Application::ExportJob::work() {
        // Reset percentage
        this->percent = 0;

        struct tm time = Utils::Time::getTmForCurrentTime();

        // Write to a temporary file first so a failed export does not truncate
        // the last successful export.
        constexpr const char * exportPath = "/switch/NX-Activity-Log/export.json";
        constexpr const char * tmpExportPath = "/switch/NX-Activity-Log/export.json.tmp";
        std::error_code ec;
        std::filesystem::remove(tmpExportPath, ec);
        std::ofstream file(tmpExportPath);
        if (!file.good()) {
            return;
        }

        file << "{\n";
        writeIndent(file, 1);
        file << "\"exportString\": ";
        writeJsonString(file, Utils::Time::tmToString(time, "%B %d %Y, %T"));
        file << ",\n";
        writeIndent(file, 1);
        file << "\"exportTimestamp\": " << Utils::Time::getTimeT(time) << ",\n";
        writeIndent(file, 1);
        file << "\"exportVersion\": ";
        writeJsonString(file, std::string(VER_STRING));
        file << ",\n";
        writeIndent(file, 1);
        file << "\"users\": [";

        // Iterate over each user
        for (size_t i = 0; i < this->app->users.size(); i++) {
            NX::User * user = this->app->users[i];

            // Add user metadata
            file << (i == 0 ? "\n" : ",\n");
            writeIndent(file, 2);
            file << "{\n";
            writeIndent(file, 3);
            file << "\"name\": ";
            writeJsonString(file, user->username());
            file << ",\n";
            writeIndent(file, 3);
            file << "\"id\": ";
            writeJsonString(file, Utils::formatHexString(user->ID().uid[0]) + Utils::formatHexString(user->ID().uid[1]));
            file << ",\n";
            writeIndent(file, 3);
            file << "\"titles\": [";
            bool wroteTitle = false;

            // Iterate over user's played titles
            for (size_t j = 0; j < this->app->titles.size(); j++) {
                NX::Title * title = this->app->titles[j];

                // Check if played, and if not move onto next
                NX::RecentPlayStatistics * stats = this->app->playdata_->getRecentStatisticsForTitleAndUser(title->titleID(), std::numeric_limits<u64>::min(), std::numeric_limits<u64>::max(), user->ID());
                bool recentLaunched = (stats->launches != 0);

                // Get all title events
                std::vector<NX::PlayEvent> events = this->app->playdata_->getPlayEvents(std::numeric_limits<u64>::min(), std::numeric_limits<u64>::max(), title->titleID(), user->ID());

                // Get all summary stats
                NX::PlayStatistics * stats2 = this->app->playdata_->getStatisticsForUser(title->titleID(), user->ID());
                bool allLaunched = (stats2->launches != 0);

                // Append title if played at least once
                if (recentLaunched || allLaunched) {
                    file << (wroteTitle ? ",\n" : "\n");
                    writeIndent(file, 4);
                    file << "{\n";
                    writeIndent(file, 5);
                    file << "\"name\": ";
                    writeJsonString(file, title->name());
                    file << ",\n";
                    writeIndent(file, 5);
                    file << "\"id\": ";
                    writeJsonString(file, Utils::formatHexString(title->titleID()));
                    file << ",\n";
                    writeIndent(file, 5);
                    file << "\"events\": [";

                    // Iterate over all events
                    for (size_t k = 0; k < events.size(); k++) {
                        std::string str;
                        switch (events[k].eventType) {
                            case NX::EventType::Applet_Launch:
                                str = "Launched";
                                break;

                            case NX::EventType::Applet_Exit:
                                str = "Closed";
                                break;

                            case NX::EventType::Applet_InFocus:
                                str = "Gained Focus";
                                break;

                            case NX::EventType::Applet_OutFocus:
                                str = "Lost Focus";
                                break;

                            case NX::EventType::Account_Active:
                                str = "Account Login";
                                break;

                            case NX::EventType::Account_Inactive:
                                str = "Account Logout";
                                break;

                            default:
                                str = "Unknown";
                                break;
                        }

                        file << (k == 0 ? "\n" : ",\n");
                        writeIndent(file, 6);
                        file << "{\n";
                        writeIndent(file, 7);
                        file << "\"clockTimestamp\": " << events[k].clockTimestamp << ",\n";
                        writeIndent(file, 7);
                        file << "\"steadyTimestamp\": " << events[k].steadyTimestamp << ",\n";
                        writeIndent(file, 7);
                        file << "\"type\": ";
                        writeJsonString(file, str);
                        file << "\n";
                        writeIndent(file, 6);
                        file << "}";
                    }
                    if (!events.empty()) {
                        file << "\n";
                        writeIndent(file, 5);
                    }
                    file << "],\n";
                    writeIndent(file, 5);
                    file << "\"summary\": {\n";
                    writeIndent(file, 6);
                    file << "\"firstPlayed\": " << stats2->firstPlayed << ",\n";
                    writeIndent(file, 6);
                    file << "\"lastPlayed\": " << stats2->lastPlayed << ",\n";
                    writeIndent(file, 6);
                    file << "\"playtime\": " << stats->playtime << ",\n";
                    writeIndent(file, 6);
                    file << "\"launches\": " << stats->launches << "\n";
                    writeIndent(file, 5);
                    file << "}\n";
                    writeIndent(file, 4);
                    file << "}";
                    wroteTitle = true;
                }

                delete stats;
                delete stats2;

                // Update percentage
                size_t current = (i * this->app->titles.size()) + j;
                size_t total = this->app->users.size() * this->app->titles.size();
                this->percent = 99 * (current/static_cast<double>(total));
            }

            // Append user
            if (wroteTitle) {
                file << "\n";
                writeIndent(file, 3);
            }
            file << "]\n";
            writeIndent(file, 2);
            file << "}";
        }
        if (!this->app->users.empty()) {
            file << "\n";
            writeIndent(file, 1);
        }
        file << "]\n";
        file << "}\n";
        file.flush();
        bool success = file.good();
        file.close();
        success = success && !file.fail();

        if (success) {
            std::filesystem::rename(tmpExportPath, exportPath, ec);
            success = !ec;
        }

        if (!success) {
            std::filesystem::remove(tmpExportPath, ec);
            return;
        }

        // Pause so those with little data can still see the process completed successfully
        this->percent = 99.9;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        this->percent = 100;
    }
};
