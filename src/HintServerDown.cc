/* Copyright (c) 2011 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <iostream>

#include "RamCloud.h"
#include "OptionParser.h"


int
main(int argc, char* argv[])
{
    using namespace RAMCloud;

    vector<string> locators;
    OptionsDescription options("HintServerDown");
    options.add_options()
        ("down,d",
         ProgramOptions::value<vector<string>>(&locators),
         "Report the specified service locator as down, "
         "can be passed multiple times for multiple reports");

    OptionParser optionParser(options, argc, argv);
    RamCloud client(optionParser.options.getCoordinatorLocator().c_str());
    foreach (const auto& locator, locators) {
        std::cout << "Hinting server down: " << locator << std::endl;
        client.coordinator.hintServerDown(locator.c_str());
    }
    return 0;
}
