#ifndef OSMIUM_UTIL_VERBOSE_OUTPUT_HPP
#define OSMIUM_UTIL_VERBOSE_OUTPUT_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013,2014 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <time.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace osmium {

    /**
     * @brief Helpful utility classes and functions not strictly OSM related
     */
    namespace util {

        /**
         * Osmium programs often run for a long time because of the amount of
         * OSM data processed. This class helps with keeping the user up to
         * date by offering an easy way for programs to optionally output
         * verbose information about what's going on.
         *
         * Use an object of this class instead of std::cerr as an output
         * stream. Nothing is actually written if the object is not set to
         * verbose mode. If it is set to verbose mode, each line is prepended
         * with the running time, ie the time since the VerboseOutput object
         * was created.
         */
        class VerboseOutput {

            /// all time output will be relative to this start time
            time_t m_start;

            /// is verbose mode enabled?
            bool m_verbose;

            /// a newline was written, start next output with runtime
            bool m_newline;

            void start_line() {
                if (m_newline) {
                    int elapsed = runtime();

                    char old_fill = std::cerr.fill();
                    std::cerr << '[' << std::setw(2) << (elapsed / 60) << ':' << std::setw(2) << std::setfill('0') << (elapsed % 60) << "] ";
                    std::cerr.fill(old_fill);

                    m_newline = false;
                }
            }

        public:

            explicit VerboseOutput(bool verbose=false) :
                m_start(time(NULL)),
                m_verbose(verbose),
                m_newline(true) {
            }

            ~VerboseOutput() {
            }

            VerboseOutput(const VerboseOutput&) = default;
            VerboseOutput& operator=(const VerboseOutput&) = default;
            VerboseOutput(VerboseOutput&&) = default;
            VerboseOutput& operator=(VerboseOutput&&) = default;

            int runtime() const {
                return time(NULL) - m_start;
            }

            /// Get "verbose" setting.
            bool verbose() const {
                return m_verbose;
            }

            /// Set "verbose" setting.
            void verbose(bool verbose) {
                m_verbose = verbose;
            }

            template<typename T>
            friend VerboseOutput& operator<<(VerboseOutput& verbose_output, T value) {
                if (verbose_output.m_verbose) {
                    std::ostringstream output_buffer;
                    output_buffer << value;
                    verbose_output.start_line();
                    std::cerr << output_buffer.str();

                    if (!output_buffer.str().empty() && output_buffer.str()[output_buffer.str().size()-1] == '\n') {
                        verbose_output.m_newline = true;
                    }
                }
                return verbose_output;
            }

        }; // class VerboseOutput

    } // namespace util

} // namespace osmium

#endif // OSMIUM_UTIL_VERBOSE_OUTPUT_HPP
