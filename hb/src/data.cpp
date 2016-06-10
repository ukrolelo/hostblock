/* 
 * Class to work with suspicious activity and log file data.
 *
 * Simple and lazy implementation would be just to rewrite whole data file on
 * each activity, but lets try to minimize I/O a little bit so read further
 * about some generic logic for data file...
 * 
 * First position means type of record, almost all data is in fixed position
 * left padded with space to fill specified len. As exception to fixed position
 * is file_path. If filename will change, old one will be marked for removal and
 * new one added to end of file. Delete replaces whole line with "r" right
 * padded with spaces until end of line. Daemon stop rewrites whole file with
 * latest data so lines starting with "r" are not saved (to get rid of them).
 * 
 * Data about suspicious activity from address:
 * d|addr|lastact|actscore|actcount|refcount|whitelisted|blacklisted
 * 
 * Log file bookmark to check for log rotation and for seekg to read only new
 * lines:
 * b|bookmark|size|file_path
 *
 * Marked for removal (any type of line), right padded with spaces according to
 * original length of line:
 * r
 * 
 * addr        - IPv4 address, len 39, current implementation for IPv4, but len
 *               for easier IPv6 implementation in future
 * lastact     - unix timestamp of last activity, len 20
 * actscore    - suspicious activity score, len 10
 * actcount    - suspicious activity count (pattern match count), len 10
 * refcount    - connection drop count, len 10
 * whitelisted - flag if this IP address is in whitelist (manual list) and
 *               should never be blocked (ignore all suspicious activity for
 *               this address), y/n, len 1
 * blacklisted - flag if this IP address is blacklisted (manual list) and must
 *               be blocked allways, y/n, len 1
 * bookmark    - bookmark with how far this file is already parsed, len 20
 * size        - size of file when it was last read, len 20
 * file_path   - full path to log file, variable len (limits.h/PATH_MAX is not
 *               reliable so no max len here)
 */

// Standard input/output stream library (cin, cout, cerr, clog, etc)
#include <iostream>
// Standard string library
#include <string>
// File stream library (ifstream)
#include <fstream>
// Parametric manipulators (setw, setfill)
#include <iomanip>
// Linux stat
namespace cstat{
	#include <errno.h>
	#include <sys/types.h>
	#include <sys/stat.h>
}
// Util
#include "util.h"
// Config
#include "config.h"
// Header
#include "data.h"

// Hostblock namespace
using namespace hb;

/*
 * Constructor
 */
Data::Data(hb::Logger* log, hb::Config* config)
: log(log), config(config)
{

}

/*
 * Read data file and store results in this->suspiciousAddresses
 * Note, config should already be processed
 */
bool Data::loadData()
{
	this->log->info("Loading data from " + this->config->dataFilePath);
	std::ifstream f(this->config->dataFilePath.c_str());
	if (f.is_open()) {
		std::string line;
		std::string recordType;
		std::string address;
		hb::SuspiciosAddressType data;
		std::pair<std::map<std::string, hb::SuspiciosAddressType>::iterator,bool> chk;
		bool duplicatesFound = false;
		unsigned long long int bookmark;
		unsigned long long int size;
		std::string logFilePath;
		bool logFileFound = false;
		std::vector<hb::LogGroup>::iterator itlg;
		std::vector<hb::LogFile>::iterator itlf;

		// Clear this->suspiciousAddresses
		this->suspiciousAddresses.clear();

		// Read data file line by line
		while (getline(f, line)) {
			// First position is record type
			recordType = line.substr(0,1);
			if(recordType == "d" && line.length() == 92){// Data about address (activity score, activity count, blacklisted, whitelisted, etc)
				// IP address
				address = hb::Util::ltrim(line.substr(1,39));
				// Timestamp of last activity
				data.lastActivity = strtoull(hb::Util::ltrim(line.substr(40,20)).c_str(), NULL, 10);
				// Total score of activity calculated at last activity
				data.activityScore = strtoul(hb::Util::ltrim(line.substr(60,10)).c_str(), NULL, 10);
				// Suspicious activity count
				data.activityCount = strtoul(hb::Util::ltrim(line.substr(70,10)).c_str(), NULL, 10);
				// Refused connection count
				data.refusedCount = strtoul(hb::Util::ltrim(line.substr(80,10)).c_str(), NULL, 10);
				// Whether IP address is in whitelist
				if (line.substr(90,1) == "y") data.whitelisted = true;
				else data.whitelisted = false;
				// Whether IP address in in blacklist
				if (line.substr(91,1) == "y") data.blacklisted = true;
				else data.blacklisted = false;
				// If IP address is in both, whitelist and blacklist, remove it from blacklist
				if (data.whitelisted == true && data.blacklisted == true) {
					this->log->warning("Address " + address + " is in whitelist and at the same time in blacklist! Removing address from blacklist...");
					data.blacklisted = false;
				}
				// When data is loaded from datafile we do not have yet info whether it has rule in iptables, this will be changed to true later if needed
				data.iptableRule = false;
				// Store in this->suspiciousAddresses
				chk = this->suspiciousAddresses.insert(std::pair<std::string, hb::SuspiciosAddressType>(address, data));
				if (chk.second == false) {
					this->log->warning("Address" + address + " is duplicated in data file, new datafile without duplicates will be created!");
					duplicatesFound = true;
				}
			} else if (recordType == "b") {// Log file bookmarks
				// Bookmark
				bookmark = strtoull(hb::Util::ltrim(line.substr(1,20)).c_str(), NULL, 10);
				// Last known size to detect if log file has been rotated
				size = strtoull(hb::Util::ltrim(line.substr(21,20)).c_str(), NULL, 10);
				// Path to log file
				logFilePath = hb::Util::rtrim(hb::Util::ltrim(line.substr(41)));
				// Update info about log file
				logFileFound = false;
				for (itlg = this->config->logGroups.begin(); itlg != this->config->logGroups.end(); ++itlg) {
					for (itlf = itlg->logFiles.begin(); itlf != itlg->logFiles.end(); ++itlf) {
						if (itlf->path == logFilePath) {
							itlf->bookmark = bookmark;
							itlf->size = size;
							logFileFound = true;
							this->log->debug("Bookmark: " + std::to_string(bookmark) + " Size: " + std::to_string(size) + " Path: " + logFilePath);
							break;
						}
					}
					if (logFileFound) break;
				}
				if (!logFileFound) {
					this->log->warning("Bookmark information in datafile for log file " + logFilePath + " found, but file not present in configuration. Removing from datafile...");
					this->removeFile(logFilePath);
				}
			}
		}

		// Finished reading file, close it
		f.close();

		// If duplicates found, rename current data file to serve as backup and save new data file without duplicates
		if (duplicatesFound) {
			// New filename for data file
			time_t rtime;
			struct tm * itime;
			time(&rtime);
			itime = localtime(&rtime);
			std::string month = std::to_string(itime->tm_mon + 1);
			if(month.length() == 1) month = "0" + month;
			std::string day = std::to_string(itime->tm_mday);
			if(day.length() == 1) day = "0" + day;
			std::string hour = std::to_string(itime->tm_hour);
			if(hour.length() == 1) hour = "0" + hour;
			std::string minute = std::to_string(itime->tm_min);
			if(minute.length() == 1) minute = "0" + minute;
			std::string second = std::to_string(itime->tm_sec);
			if(second.length() == 1) second = "0" + second;
			std::string newDataFileName = this->config->dataFilePath + "_" + std::to_string(itime->tm_year + 1900) + month + day + hour + minute + second + ".bck";
			// Check if new filename doesn't exist (so that it is not overwritten)
			struct cstat::stat buffer;
			if (cstat::stat(newDataFileName.c_str(), &buffer) != 0) {
				if (std::rename(this->config->dataFilePath.c_str(), newDataFileName.c_str()) != 0) {
					this->log->error("Current data file contains duplicate entries and backup creation failed (file rename failure)!");
					return false;
				}
			} else{
				this->log->error("Current data file contains duplicate entries and backup creation failed (backup with same name already exists)!");
				return false;
			}
			// Save data without duplicates to data file (create new data file)
			if (this->saveData() == false) {
				this->log->error("Current data file contains duplicate entries, renamed data file successfully, but failed to save new data file!");
				return false;
			}
			this->log->warning("Duplicate data found while reading data file! Old data file stored as " + newDataFileName + ", new data file without duplicates saved! Merge manually if needed.");
		}

		this->log->info("Loaded " + std::to_string(this->suspiciousAddresses.size()) + " IP address record(s)");
	} else {
		this->log->warning("Unable to open datafile for reading!");
		if (this->saveData() == false) {
			this->log->error("Unable to create new empty data file!");
			return false;
		}
	}

	return true;
}

/*
 * Save this->suspiciousAddresses to data file, will replace if file already exists
 */
bool Data::saveData()
{
	this->log->info("Updating data in " + this->config->dataFilePath);
	// Open file
	std::ofstream f(this->config->dataFilePath.c_str());
	if (f.is_open()) {
		// Loop through all addresses
		std::map<std::string, SuspiciosAddressType>::iterator it;
		for (it = this->suspiciousAddresses.begin(); it!=this->suspiciousAddresses.end(); ++it) {
			f << "d";
			f << std::right << std::setw(39) << it->first;// Address, left padded with spaces
			f << std::right << std::setw(20) << it->second.lastActivity;// Last activity, left padded with spaces
			f << std::right << std::setw(10) << it->second.activityScore;// Current activity score, left padded with spaces
			f << std::right << std::setw(10) << it->second.activityCount;// Total activity count, left padded with spaces
			f << std::right << std::setw(10) << it->second.refusedCount;// Total refused connection count, left padded with spaces
			if(it->second.whitelisted == true) f << "y";
			else f << "n";
			if(it->second.blacklisted == true) f << "y";
			else f << "n";
			// f << std::endl;// endl should flush buffer
			f << "\n";// \n should not flush buffer
		}

		// Loop all log files
		std::vector<hb::LogGroup>::iterator itlg;
		std::vector<hb::LogFile>::iterator itlf;
		for (itlg = this->config->logGroups.begin(); itlg != this->config->logGroups.end(); ++itlg) {
			for (itlf = itlg->logFiles.begin(); itlf != itlg->logFiles.end(); ++itlf) {
				f << "b";
				f << std::right << std::setw(20) << itlf->bookmark;
				f << std::right << std::setw(20) << itlf->size;
				f << itlf->path;
				// f << std::endl;// endl should flush buffer
				f << "\n";// \n should not flush buffer
			}
		}

		// Close datafile
		f.close();
	} else {
		this->log->error("Unable to open datafile for writting!");
		return false;
	}
	return true;
}

/*
 * Add new record to datafile end based on this->suspiciousAddresses
 */
bool Data::addAddress(std::string address)
{
	this->log->debug("Adding record to " + this->config->dataFilePath + ", adding address " + address);
	std::ofstream f(this->config->dataFilePath.c_str(), std::ofstream::out | std::ofstream::app);
	if (f.is_open()) {
		// Write record to datafile end
		f << "d";
		f << std::right << std::setw(39) << address;// Address, left padded with spaces
		f << std::right << std::setw(20) << this->suspiciousAddresses[address].lastActivity;// Last activity, left padded with spaces
		f << std::right << std::setw(10) << this->suspiciousAddresses[address].activityScore;// Current activity score, left padded with spaces
		f << std::right << std::setw(10) << this->suspiciousAddresses[address].activityCount;// Total activity count, left padded with spaces
		f << std::right << std::setw(10) << this->suspiciousAddresses[address].refusedCount;// Total refused connection count, left padded with spaces
		if(this->suspiciousAddresses[address].whitelisted == true) f << "y";
		else f << "n";
		if(this->suspiciousAddresses[address].blacklisted == true) f << "y";
		else f << "n";
		// f << std::endl;// endl should flush buffer
		f << "\n";// \n should not flush buffer
		// Close datafile
		f.close();
	} else {
		this->log->error("Unable to open datafile for writting!");
		return false;
	}

	return true;
}

/*
 * Update record in datafile based on this->suspiciousAddresses
 */
bool Data::updateAddress(std::string address)
{
	bool recordFound = false;
	char c;
	char fAddress[40];
	// std:string buffer;
	this->log->debug("Updating record in " + this->config->dataFilePath + ", updating address " + address);
	std::fstream f(this->config->dataFilePath.c_str(), std::fstream::in | std::fstream::out);
	if (f.is_open()) {
		while (f.get(c)) {
			// std::cout << "Record type: " << c << " tellg: " << std::to_string(f.tellg()) << std::endl;
			if (c == 'd') {// Data record, check if IP matches
				// Get address
				f.get(fAddress, 40);
				// If we have found address that we need to update
				if (hb::Util::ltrim(std::string(fAddress)) == address) {
					f << std::right << std::setw(20) << this->suspiciousAddresses[address].lastActivity;// Last activity, left padded with spaces
					f << std::right << std::setw(10) << this->suspiciousAddresses[address].activityScore;// Current activity score, left padded with spaces
					f << std::right << std::setw(10) << this->suspiciousAddresses[address].activityCount;// Total activity count, left padded with spaces
					f << std::right << std::setw(10) << this->suspiciousAddresses[address].refusedCount;// Total refused connection count, left padded with spaces
					if(this->suspiciousAddresses[address].whitelisted == true) f << "y";
					else f << "n";
					if(this->suspiciousAddresses[address].blacklisted == true) f << "y";
					else f << "n";
					// f << std::endl;// endl should flush buffer
					f << "\n";// \n should not flush buffer
					recordFound = true;
					break;// No need to continue reading file
				}
				// std::cout << "Address: " << hb::Util::ltrim(std::string(fAddress)) << " tellg: " << std::to_string(f.tellg()) << std::endl;
				f.seekg(53, f.cur);
			} else {// Other type of record (bookmark or removed record)
				// We can skip at min 41 pos
				f.seekg(41, f.cur);
				// Read until end of line
				while (f.get(c)) {
					if (c == '\n') {
						break;
					}
				}
			}
		}

		// Close data file
		f.close();
	} else {
		this->log->error("Unable to open datafile for update!");
		return false;
	}

	if (!recordFound) {
		this->log->error("Unable to update " + address + " in data file, record not found in data file!");
		// Maybe better write warning, backup existing datafile and create new one based on data in memory?
		return false;
	} else {
		return true;
	}
}

/*
 * Mark record for removal in datafile
 */
bool Data::removeAddress(std::string address)
{
	bool recordFound = false;
	char c;
	char fAddress[40];
	// std:string buffer;
	this->log->debug("Removing record from " + this->config->dataFilePath + ", removing address " + address);
	std::fstream f(this->config->dataFilePath.c_str(), std::fstream::in | std::fstream::out);
	if (f.is_open()) {
		while (f.get(c)) {
			// std::cout << "Record type: " << c << " tellg: " << std::to_string(f.tellg()) << std::endl;
			if (c == 'd') {// Data record, check if IP matches
				// Get address
				f.get(fAddress, 40);
				// If we have found address that we need to remove
				if (hb::Util::ltrim(std::string(fAddress)) == address) {
					f.seekg(-40, f.cur);
					f << "r";
					recordFound = true;
					break;// No need to continue reading file
				}
				// std::cout << "Address: " << hb::Util::ltrim(std::string(fAddress)) << " tellg: " << std::to_string(f.tellg()) << std::endl;
				f.seekg(53, f.cur);
			} else {// Other type of record (bookmark or removed record)
				// We can skip at min 41 pos
				f.seekg(41, f.cur);
				// Read until end of line
				while (f.get(c)) {
					if (c == '\n') {
						break;
					}
				}
			}
		}

		// Close data file
		f.close();
	} else {
		this->log->error("Unable to open datafile for update!");
		return false;
	}

	if (!recordFound) {
		this->log->error("Tried removing address " + address + " from datafile, but record is not present in datafile!");
		// Maybe better write warning, backup existing datafile and create new one based on data in memory?
		return false;
	} else {
		return true;
	}
	return false;
}

/*
 * Add new log file bookmark record to datafile
 */
bool Data::addFile(std::string filePath)
{

	return false;
}

/*
 * Update log file bookmark record in datafile
 */
bool Data::updateFile(std::string filePath)
{

	return false;
}

/*
 * Mark log file bookmark record for removal in datafile
 */
bool Data::removeFile(std::string filePath)
{

	return false;
}
