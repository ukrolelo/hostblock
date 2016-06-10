/* 
 * Class to work with data file.
 */
#ifndef HBDATA_H
#define HBDATA_H

// Map
#include <map>
// String
#include <string>
// Logger
#include "logger.h"
// Config
#include "config.h"
// Util
#include "util.h"

namespace hb{

class Data{
	private:

	public:
		/*
		 * Logger object
		 */
		hb::Logger* log;

		/*
		 * Config object
		 */
		hb::Config* config;

		/*
		 * Data about suspicious, whitelisted and blacklisted addresses
		 */
		std::map<std::string, hb::SuspiciosAddressType> suspiciousAddresses;

		/*
		 * Constructor
		 */
		Data(hb::Logger* log, hb::Config* config);

		/*
		 * Read data file and store results in this->suspiciousAddresses
		 */
		bool loadData();

		/*
		 * Save this->suspiciousAddresses to data file, will replace if file already exists
		 * Warhing, this rewrites whole file, should not be used for single record updates
		 * Instead there are separate methods for single record updates that replace only single line in file
		 * This method is intended only to keep data file cleaner, to use this on normal shutdown
		 */
		bool saveData();

		/*
		 * Add new record to datafile based on this->suspiciousAddresses
		 */
		bool addAddress(std::string address);

		/*
		 * Update record in datafile based on this->suspiciousAddresses
		 */
		bool updateAddress(std::string address);

		/*
		 * Mark record for removal in datafile
		 */
		bool removeAddress(std::string address);

		/*
		 * Add new log file bookmark record to datafile
		 */
		bool addFile(std::string filePath);

		/*
		 * Update log file bookmark record in datafile
		 */
		bool updateFile(std::string filePath);

		/*
		 * Mark log file bookmark record for removal in datafile
		 */
		bool removeFile(std::string filePath);
};

}

#endif
