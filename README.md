This C++ version of `wikiq` in this repository has not be updated since ~2011
and has a number of critical limitations. The repository is being kept here for
historical and archival purposes. Please don't rely on it! 

**A improved version of a very similar stream-based XML-parser for MediaWiki by
the same authors can be found here:**

> **[https://code.communitydata.cc/mediawiki\_dump\_tools.git](https://code.communitydata.cc/mediawiki_dump_tools.git)**

These new tools are maintained by some of the same authors (now based in the
[Community Data Science Collective](https://communitydata.cc)) and the new tool
relies on many of the same libraries including the `expat` non-validating XML
parser.

This new version has a very similar interface, is in written in Python, and
leverages [Python MediaWiki Utilities](https://github.com/mediawiki-utilities)
for XML dump parsing and several other tasks. The two tools have been
benchmarked and the new tool's performance measures are generally within 90% of
the C++ version of tool in this repository.

>> —[Benjamin Mako Hill](https://mako.cc/)

