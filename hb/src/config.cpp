/* 
 * Class to work with hostblock config file.
 */

// Standard input/output stream library (cin, cout, cerr, clog, etc)
#include <iostream>
// Standard string library
#include <string>
// File stream library (ifstream)
#include <fstream>
// Time library (time_t, time, localtime)
#include <time.h>
// Logger
#include "logger.h"
// Util
#include "util.h"
// Header
#include "config.h"

// Hostblock namespace
using namespace hb;

/*
 * Constructor
 */
Config::Config(hb::Logger log)
: log(&log)
{

}
Config::Config(hb::Logger log, std::string configPath)
: log(&log), configPath(configPath)
{

}

/*
 * Load configuration from file
 */
bool Config::load()
{
	this->log->debug("Loading config from " + this->configPath);
	std::ifstream f(this->configPath);
	std::string line;
	int group = 0;// 0 Global group, 1 Log group
	std::vector<hb::LogGroup>::iterator itlg;
	std::vector<hb::Pattern>::iterator itp;
	size_t pos;
	bool logDetails = true;

	try{
		std::smatch groupSearchResults;
		std::regex groupSearchPattern("\\[\\S+\\]");

		// Read config file line by line
		while (getline(f, line)) {
			// Trim spaces from line
			line = hb::Util::rtrim(hb::Util::ltrim(line));

			// Skip lines that start with # (comments)
			if (line.length() > 0 && line[0] != '#') {

				// Remove comment if it is on same line
				pos = line.find_first_of("#");
				if (pos != std::string::npos) {
					line = hb::Util::rtrim(line.substr(0, pos - 1));
				}

				// Group handling - Gobal and Log.*
				if (regex_search(line, groupSearchResults, groupSearchPattern) && groupSearchResults.size() == 1) {
					if (line.substr(1,6) == "Global") {
						group = 0;
					} else if (line.substr(1,4) == "Log.") {
						group = 1;
						hb::LogGroup logGroup;
						logGroup.name = line.substr(5,line.length()-6);
						if (logDetails) this->log->debug("Log file group: " + logGroup.name);
						itlg = this->logGroups->insert(itlg, logGroup);
					}
				}

				if (group == 0) {// Global section
					if (line.substr(0,18) == "log.check.interval") {
						pos = line.find_first_of("=");
						if (pos != std::string::npos) {
							line = hb::Util::ltrim(line.substr(pos+1));
							this->logCheckInterval = strtoul(line.c_str(), NULL, 10);
							if (logDetails) this->log->debug("Interval for log file check: " + std::to_string(this->logCheckInterval));
						}
					} else if (line.substr(0,19) == "address.block.score") {
						pos = line.find_first_of("=");
						if (pos != std::string::npos) {
							line = hb::Util::ltrim(line.substr(pos+1));
							this->activityScoreToBlock = strtoul(line.c_str(), NULL, 10);
							if (logDetails) this->log->debug("Needed score to block IP address: " + std::to_string(this->activityScoreToBlock));
						}
					} else if (line.substr(0,24) == "address.block.multiplier") {
						pos = line.find_first_of("=");
						if (pos != std::string::npos) {
							line = hb::Util::ltrim(line.substr(pos+1));
							this->keepBlockedScoreMultiplier = strtoul(line.c_str(), NULL, 10);
							if (logDetails) this->log->debug("Score multiplier for rule keeping: " + std::to_string(this->keepBlockedScoreMultiplier));
						}
					} else if (line.substr(0,13) == "datafile.path") {
						pos = line.find_first_of("=");
						if (pos != std::string::npos) {
							this->dataFilePath = hb::Util::ltrim(line.substr(pos+1));
							if (logDetails) this->log->debug("Datafile path: " + this->dataFilePath);
						}
					}
				} else if (group == 1) {// Log group section
					if (line.substr(0,8) == "log.path") {
						pos = line.find_first_of("=");
						if (pos != std::string::npos) {
							hb::LogFile logFile;
							logFile.path = hb::Util::ltrim(line.substr(pos+1));
							itlg->logFiles.push_back(logFile);
							if (logDetails) this->log->debug("Logfile path: " + hb::Util::ltrim(line.substr(pos+1)));
						}
					} else if (line.substr(0,11) == "log.pattern") {
						pos = line.find_first_of("=");
						if (pos != std::string::npos) {
							hb::Pattern pattern;
							pattern.pattern = hb::Util::ltrim(line.substr(pos+1));
							// TODO, %i is mandatory in pattern, if not present then skip that rule and write warning to syslog
							itp = itlg->patterns.end();
							itp = itlg->patterns.insert(itp,pattern);
							if (logDetails) this->log->debug("Pattern to match: " + pattern.pattern);
						}
					} else if (line.substr(0,9) == "log.score") {
						pos = line.find_first_of("=");
						if (pos != std::string::npos) {
							line = hb::Util::ltrim(line.substr(pos+1));
							itp->score = strtoul(line.c_str(), NULL, 10);
							if (logDetails) this->log->debug("Score for previous pattern: " + std::to_string(itp->score));
						}
					}
				}
			}
		}
	} catch (std::regex_error& e){
		std::string message = e.what();
		this->log->error(message + ": " + std::to_string(e.code()));
		this->log->error(Util::regexErrorCode2Text(e.code()));
	}
	return true;
}

/*
 * Print (stdout) currently loaded config
 */
void Config::print()
{
	time_t currentTime;
	time(&currentTime);
	std::cout << "## Hostblock configuration, generated automatically" << std::endl;
	std::cout << "## Timestamp: " << currentTime << std::endl << std::endl;
	std::cout << "[General]" << std::endl << std::endl;
	std::cout << "## Interval for log file check (seconds, default 30)" << std::endl;
	std::cout << "log.check.interval = " << this->logCheckInterval << std::endl << std::endl;
	std::cout << "## Needed score to block IP address (default 10)" << std::endl;
	std::cout << "address.block.score = " << this->activityScoreToBlock << std::endl << std::endl;
	std::cout << "## Score multiplier to calculate how long to keep iptable rules (seconds, default 3600, 0 will not remove)" << std::endl;
	std::cout << "address.block.multiplier = " << this->keepBlockedScoreMultiplier << std::endl << std::endl;
	std::cout << "## Full path to datafile" << std::endl;
	std::cout << "datafile.path = " << this->dataFilePath << std::endl << std::endl;
	std::vector<LogGroup>::iterator itlg;
	std::vector<LogFile>::iterator itlf;
	std::vector<Pattern>::iterator itpa;
	for (itlg = this->logGroups->begin(); itlg != this->logGroups->end(); ++itlg) {
		std::cout << std::endl << "## Pattern and log file configuration for " << itlg->name << std::endl;
		std::cout << "[Log." << itlg->name << "]" << std::endl << std::endl;
		std::cout << "## Path to log file(s)" << std::endl;
		for (itlf = itlg->logFiles.begin(); itlf != itlg->logFiles.end(); ++itlf) {
			std::cout << "## " << itlf->bookmark << std::endl;
			std::cout << "## " << itlf->size << std::endl;
			std::cout << "log.path = " << itlf->path << std::endl << std::endl;
		}
		std::cout << "## Patterns to match with scores to use for calculation" << std::endl;
		std::cout << "## Include %i in pattern, will search there for IP address" << std::endl;
		std::cout << "## Specify score after each pattern, if not specified by default will be set 1" << std::endl;
		for (itpa = itlg->patterns.begin(); itpa != itlg->patterns.end(); ++itpa) {
			std::cout << "log.pattern = " << itpa->pattern << std::endl;
			std::cout << "log.score = " << itpa->score << std::endl;
		}
	}
}
