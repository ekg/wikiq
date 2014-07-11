/* 
 * An XML parser for Wikipedia Data dumps.
 * Converts XML files to tab-separated values files readable by spreadsheets
 * and statistical packages.
 */

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "expat.h"
#include <getopt.h>
#include "md5.h"
#include "dtl/dtl.hpp"
#include <vector>
#include <map>
#include <pcrecpp.h>


using namespace std;

// timestamp of the form 2003-11-07T00:43:23Z
#define DATE_LENGTH 10
#define TIME_LENGTH 8
#define TIMESTAMP_LENGTH 20

#define MEGABYTE 1048576
#define FIELD_BUFFER_SIZE 1024

// this can be changed at runtime if we encounter an article larger than 10mb
size_t text_buffer_size = 10 * MEGABYTE;

enum elements { 
    TITLE, ARTICLEID, REVISION, REVID, TIMESTAMP, CONTRIBUTOR, 
    EDITOR, EDITORID, MINOR, COMMENT, UNUSED, TEXT
}; 

enum block { TITLE_BLOCK, REVISION_BLOCK, CONTRIBUTOR_BLOCK, SKIP };

enum outtype { FULL, SIMPLE };

typedef struct {

    // pointers to once-allocated buffers
    char *title;
    char *articleid;
    char *revid;
    char *date;
    char *time;
    char *timestamp;
    char *anon;
    char *editor;
    char *editorid;
    char *comment;
    char *text;
    vector<string> last_text_tokens;

    // title regexes
    vector<pcrecpp::RE> title_regexes;

    // regexes for checking with revisions
    vector<string> content_regex_names;
    vector<pcrecpp::RE> content_regexes;

    // regexes for looking within diffs
    vector<string> diff_regex_names;
    vector<pcrecpp::RE> diff_regexes;

    map<string, string> revision_md5; // used for detecting reversions

    // track string size of the elements, to prevent O(N^2) processing in charhndl
    // when we have to take strlen for every character which we append to the buffer
    size_t title_size;
    size_t articleid_size;
    size_t revid_size;
    size_t date_size;
    size_t time_size;
    size_t timestamp_size;
    size_t anon_size;
    size_t editor_size;
    size_t editorid_size;
    size_t comment_size;
    size_t text_size;

    bool minor;
    
    enum elements element;
    enum block position;
    enum outtype output_type;
    
} revisionData;


/* free_data and clean_data
 * Takes a pointer to the data struct and an integer {0,1} indicating if the 
 * title data needs to be cleared as well.
 * Also, frees memory dynamically allocated to store data.
 */ 
static void
clean_data(revisionData *data, int title)
{
    // reset title (if we are switching articles)
    if (title) {
        data->title[0] = '\0';
        data->articleid[0] = '\0';
        data->title_size = 0;
        data->articleid_size = 0;
    }

    // reset text fields
    data->revid[0] = '\0';
    data->date[0] = '\0';
    data->time[0] = '\0';
    data->timestamp[0] = '\0';
    data->anon[0] = '\0';
    data->editor[0] = '\0';
    data->editorid[0] = '\0';
    data->comment[0] = '\0';
    data->text[0] = '\0';

    // reset length tracking
    data->revid_size = 0;
    data->date_size = 0;
    data->time_size = 0;
    data->timestamp_size = 0;
    data->anon_size = 0;
    data->editor_size = 0;
    data->editorid_size = 0;
    data->comment_size = 0;
    data->text_size = 0;

    // reset flags and element type info
    data->minor = false;
    data->element = UNUSED;

}

// presently unused
static void
free_data(revisionData *data, int title)
{
    if (title) {
        //printf("freeing article\n");
        free(data->title);
        free(data->articleid);
    }
    free(data->revid);
    free(data->date);
    free(data->time);
    free(data->timestamp);
    free(data->anon);
    free(data->editor);
    free(data->editorid);
    free(data->comment);
    free(data->text);
    data->last_text_tokens.clear();
}

void cleanup_revision(revisionData *data) {
    clean_data(data, 0);
}

void cleanup_article(revisionData *data) {
    clean_data(data, 1);
    data->last_text_tokens.clear();
    data->revision_md5.clear();
}


static void 
init_data(revisionData *data, outtype output_type)
{
    data->text = (char*) malloc(text_buffer_size);
    data->comment = (char*) malloc(FIELD_BUFFER_SIZE);
    data->title = (char*) malloc(FIELD_BUFFER_SIZE);
    data->articleid = (char*) malloc(FIELD_BUFFER_SIZE);
    data->revid = (char*) malloc(FIELD_BUFFER_SIZE);
    data->date = (char*) malloc(FIELD_BUFFER_SIZE);
    data->time = (char*) malloc(FIELD_BUFFER_SIZE);
    data->timestamp = (char*) malloc(FIELD_BUFFER_SIZE);
    data->anon = (char*) malloc(FIELD_BUFFER_SIZE);
    data->editor = (char*) malloc(FIELD_BUFFER_SIZE);
    data->editorid = (char*) malloc(FIELD_BUFFER_SIZE);
    data->minor = false;

    // resets the data fields, null terminates strings, sets lengths
    clean_data(data, 1);

    data->output_type = output_type;
}

/* for debugging only, prints out the state of the data struct
 */
static void
print_state(revisionData *data) 
{
    printf("element = %i\n", data->element);
    printf("output_type = %i\n", data->output_type);
    printf("title = %s\n", data->title);
    printf("articleid = %s\n", data->articleid);
    printf("revid = %s\n", data->revid);
    printf("date = %s\n", data->date);
    printf("time = %s\n", data->time);
    printf("anon = %s\n", data->anon);
    printf("editor = %s\n", data->editor);
    printf("editorid = %s\n", data->editorid);
    printf("minor = %s\n", (data->minor ? "1" : "0"));
    printf("comment = %s\n", data->comment); 
    printf("text = %s\n", data->text);
    printf("\n");

}


/* 
 * write a line of comma-separated value formatted data to standard out
 * follows the form:
 * title,articleid,revid,date,time,anon,editor,editorid,minor,comment
 * (str)  (int)    (int) (str)(str)(bin)(str)   (int)   (bin) (str)
 *
 * it is called right before cleanup_revision() and cleanup_article()
 */
static void
write_row(revisionData *data)
{

    // get md5sum
    md5_state_t state;
    md5_byte_t digest[16];
    char md5_hex_output[2 * 16 + 1];
    md5_init(&state);
    md5_append(&state, (const md5_byte_t *)data->text, data->text_size);
    md5_finish(&state, digest);
    int di;
    for (di = 0; di < 16; ++di) {
        sprintf(md5_hex_output + di * 2, "%02x", digest[di]);
    }

    string reverted_to;
    map<string, string>::iterator prev_revision = data->revision_md5.find(md5_hex_output);
    if (prev_revision != data->revision_md5.end()) {
        reverted_to = prev_revision->second; // id of previous revision
    }
    data->revision_md5[md5_hex_output] = data->revid;

    string text = string(data->text, data->text_size);
    vector<string> text_tokens;
    size_t pos = 0;
    size_t start = 0;
    while ((pos = text.find_first_of(" \n\t\r", pos)) != string::npos) {
        //cout << "\"\"\"" << text.substr(start, pos - start) << "\"\"\"" << endl;
        text_tokens.push_back(text.substr(start, pos - start));
        start = pos;
        ++pos;
    }

    // look to see if (a) we've passed in a list of /any/ title_regexes
    // and (b) if all of the title_regex_matches match
    // if (a) is true and (b) is not, we return
    bool any_title_regex_match = false;
    if (!data->title_regexes.empty()) {
        for (vector<pcrecpp::RE>::iterator r = data->title_regexes.begin(); r != data->title_regexes.end(); ++r) {
            pcrecpp::RE& title_regex = *r;
            if (title_regex.PartialMatch(data->title)) {
                any_title_regex_match = true;
                break;
            }
        }
        if (!any_title_regex_match) {
            return;
        }
    }

    // search the content of the revision for a any of the regexes
    vector<bool> content_regex_matches;
    if (!data->content_regexes.empty()) {
        for (vector<pcrecpp::RE>::iterator r = data->content_regexes.begin(); r != data->content_regexes.end(); ++r) {
            pcrecpp::RE& content_regex = *r;
            content_regex_matches.push_back(content_regex.PartialMatch(data->text));
        }
    }

    //vector<string> additions;
    //vector<string> deletions;
    string additions;
    string deletions;

    vector<bool> diff_regex_matches_adds;
    vector<bool> diff_regex_matches_dels;

    if (data->last_text_tokens.empty()) {
        additions = data->text;
    } else {
        // do the diff
        
        dtl::Diff< string, vector<string> > d(data->last_text_tokens, text_tokens);
        //d.onOnlyEditDistance();
        d.compose();

        vector<pair<string, dtl::elemInfo> > ses_v = d.getSes().getSequence();
        for (vector<pair<string, dtl::elemInfo> >::iterator sit=ses_v.begin(); sit!=ses_v.end(); ++sit) {
            switch (sit->second.type) {
            case dtl::SES_ADD:
                //cout << "ADD: \"" << sit->first << "\"" << endl;
                additions += sit->first;
                break;
            case dtl::SES_DELETE:
                //cout << "DEL: \"" << sit->first << "\"" << endl;
                deletions += sit->first;
                break;
            }
        }
    }
    
    if (!additions.empty()) {
        //cout << "ADD: " << additions << endl;
        for (vector<pcrecpp::RE>::iterator r = data->diff_regexes.begin(); r != data->diff_regexes.end(); ++r) {
            pcrecpp::RE& diff_regex = *r;
            diff_regex_matches_adds.push_back(diff_regex.PartialMatch(additions));
        }
    }

    if (!deletions.empty()) {
        //cout << "DEL: " << deletions << endl;
        for (vector<pcrecpp::RE>::iterator r = data->diff_regexes.begin(); r != data->diff_regexes.end(); ++r) {
            pcrecpp::RE& diff_regex = *r;
            diff_regex_matches_dels.push_back(diff_regex.PartialMatch(deletions));
        }
    }

    data->last_text_tokens = text_tokens;


    // print line of tsv output
    cout
        << data->title << "\t"
        << data->articleid << "\t"
        << data->revid << "\t"
        << data->date << " "
        << data->time << "\t"
        << ((data->editor[0] != '\0') ? "FALSE" : "TRUE") << "\t"
        << data->editor << "\t"
        << data->editorid << "\t"
        << ((data->minor) ? "TRUE" : "FALSE") << "\t"
        << (unsigned int) data->text_size << "\t"
        << md5_hex_output << "\t"
        << reverted_to << "\t"
        << (int) additions.size() << "\t"
        << (int) deletions.size();

    for (int n = 0; n < data->content_regex_names.size(); ++n) {
        cout << "\t" << ((!content_regex_matches.empty()
			  && content_regex_matches.at(n)) ? "TRUE" : "FALSE");
    }

    for (int n = 0; n < data->diff_regex_names.size(); ++n) {
        cout << "\t" << ((!diff_regex_matches_adds.empty() && diff_regex_matches_adds.at(n)) ? "TRUE" : "FALSE")
             << "\t" << ((!diff_regex_matches_dels.empty() && diff_regex_matches_dels.at(n)) ? "TRUE" : "FALSE");
    }
    cout << endl;

    // 
    if (data->output_type == FULL) {
        cout << "comment:" << data->comment << endl
             << "text:" << endl << data->text << endl;
    }

}

void
split_timestamp(revisionData *data) 
{
    char *t = data->timestamp;
    strncpy(data->date, data->timestamp, DATE_LENGTH);
    char *timeinstamp = &data->timestamp[DATE_LENGTH+1];
    strncpy(data->time, timeinstamp, TIME_LENGTH);
}

// like strncat but with previously known length
char*
strlcatn(char *dest, const char *src, size_t dest_len, size_t n)
{
   size_t i;

   for (i = 0 ; i < n && src[i] != '\0' ; i++)
       dest[dest_len + i] = src[i];
   dest[dest_len + i] = '\0';

   return dest;
}

static void
charhndl(void* vdata, const XML_Char* s, int len)
{ 
    revisionData* data = (revisionData*) vdata;
    size_t bufsz;
    if (data->element != UNUSED && data->position != SKIP) {
        switch (data->element) {
            case TEXT:
                    // check if we'd overflow our buffer
                    bufsz = data->text_size + len;
                    if (bufsz + 1 > text_buffer_size) {
                        data->text = (char*) realloc(data->text, bufsz + 1);
                        text_buffer_size = bufsz + 1;
                    }
                    strlcatn(data->text, s, data->text_size, len);
                    data->text_size = bufsz;
                    break;
            case COMMENT:
                    strlcatn(data->comment, s, data->comment_size, len);
                    data->comment_size += len;
                    break;
            case TITLE:
                    strlcatn(data->title, s, data->title_size, len);
                    data->title_size += len;
                    break;
            case ARTICLEID:
                   // printf("articleid = %s\n", t);
                    strlcatn(data->articleid, s, data->articleid_size, len);
                    data->articleid_size += len;
                    break;
            case REVID:
                   // printf("revid = %s\n", t);
                    strlcatn(data->revid, s, data->revid_size, len);
                    data->revid_size += len;
                    break;
            case TIMESTAMP: 
                    strlcatn(data->timestamp, s, data->timestamp_size, len);
                    data->timestamp_size += len;
                    if (strlen(data->timestamp) == TIMESTAMP_LENGTH)
                        split_timestamp(data);
                    break;
            case EDITOR:
                    strlcatn(data->editor, s, data->editor_size, len);
                    data->editor_size += len;
                    break;
            case EDITORID: 
                    //printf("editorid = %s\n", t);
                    strlcatn(data->editorid, s, data->editorid_size, len);
                    data->editorid_size += len;
                    break;
            /* the following are implied or skipped:
            case MINOR: 
                    printf("found minor element\n");  doesn't work
                    break;                   minor tag is just a tag
            case UNUSED: 
            */
            default: break;
        }
    }
}

static void
start(void* vdata, const XML_Char* name, const XML_Char** attr)
{
    revisionData* data = (revisionData*) vdata;
    
    if (strcmp(name,"title") == 0) {
        cleanup_article(data); // cleans up data from last article
        data->element = TITLE;
        data->position = TITLE_BLOCK;
    } else if (data->position != SKIP) {
        if (strcmp(name,"revision") == 0) {
            data->element = REVISION;
            data->position = REVISION_BLOCK;
        } else if (strcmp(name, "contributor") == 0) {
            data->element = CONTRIBUTOR;
            data->position = CONTRIBUTOR_BLOCK;
        } else if (strcmp(name,"id") == 0)
            switch (data->position) {
                case TITLE_BLOCK:
                    data->element = ARTICLEID;
                    break;
                case REVISION_BLOCK: 
                    data->element = REVID;
                    break;
                case CONTRIBUTOR_BLOCK:
                    data->element = EDITORID;
                    break;
            }
    
        // minor tag has no character data, so we parse here
        else if (strcmp(name,"minor") == 0) {
            data->element = MINOR;
            data->minor = true; 
        }
        else if (strcmp(name,"timestamp") == 0)
            data->element = TIMESTAMP;

        else if (strcmp(name, "username") == 0)
            data->element = EDITOR;

        else if (strcmp(name,"ip") == 0) 
            data->element = EDITORID;

        else if (strcmp(name,"comment") == 0)
            data->element = COMMENT;

        else if (strcmp(name,"text") == 0)
            data->element = TEXT;

        else if (strcmp(name,"page") == 0 
                || strcmp(name,"mediawiki") == 0
                || strcmp(name,"restrictions") == 0
                || strcmp(name,"siteinfo") == 0)
            data->element = UNUSED;
    }

}


static void
end(void* vdata, const XML_Char* name)
{
    revisionData* data = (revisionData*) vdata;
    if (strcmp(name, "revision") == 0 && data->position != SKIP) {
        write_row(data); // crucial... :)
        cleanup_revision(data);  // also crucial
    } else {
        data->element = UNUSED; // sets our state to "not-in-useful"
    }                           // thus avoiding unpleasant character data 
                                // b/w tags (newlines etc.)
}

void print_usage(char* argv[]) {
    cerr << "usage: <wikimedia dump xml> | " << argv[0] << "[options]" << endl
         << endl
         << "options:" << endl
         << "  -v   verbose mode prints text and comments after each line of tab separated data" << endl
         << "  -n   name of the following regex for content (e.g. -n name -r \"...\")" << endl
         << "  -r   regex to check against content of the revision" << endl
         << "  -N   name of the following regex for diffs (e.g. -N name -R \"...\")" << endl
         << "  -R   regex to check against diffs (i.e., additions and deletions)" << endl
         << "  -t   parse revisions only from pages whose titles match regex(es)" << endl
         << endl
         << "Takes a wikimedia data dump XML stream on standard in, and produces" << endl
         << "a tab-separated stream of revisions on standard out:" << endl
         << endl
         << "title, articleid, revid, timestamp, anon, editor, editorid, minor," << endl
         << "text_length, text_md5, reversion, additions_size, deletions_size" << endl
         << ".... and additional fields for each regex executed against add/delete diffs" << endl
         << endl
         << "Boolean fields are TRUE/FALSE except in the case of reversion, which is blank" << endl
         << "unless the article is a revert to a previous revision, in which case, it" << endl
         << "contains the revision ID of the revision which was reverted to." << endl
         << endl
         << "authors: Erik Garrison <erik@hypervolu.me>" << endl;
         << "         Benjamin Mako Hill <mako@atdot.cc>" << endl;
}


int
main(int argc, char *argv[])
{
    
    enum outtype output_type;
    int dry_run = 0;
    // in "simple" output, we don't print text and comments
    output_type = SIMPLE;
    char c;
    string diff_regex_name;
    string content_regex_name;

    // the user data struct which is passed to callback functions
    revisionData data;

    while ((c = getopt(argc, argv, "hvn:r:t:")) != -1)
        switch (c)
        {
            case 'd':
                dry_run = 1;
                break;
            case 'v':
                output_type = FULL;
                break;
            case 'n':
                content_regex_name = optarg;
                break;
            case 'r':
                data.content_regexes.push_back(pcrecpp::RE(optarg, pcrecpp::UTF8()));
                data.content_regex_names.push_back(content_regex_name);
                if (!content_regex_name.empty()) {
                    content_regex_name.clear();
                }
                break;
            case 'N':
                diff_regex_name = optarg;
                break;
            case 'R':
                data.diff_regexes.push_back(pcrecpp::RE(optarg, pcrecpp::UTF8()));
                data.diff_regex_names.push_back(diff_regex_name);
                if (!diff_regex_name.empty()) {
                    diff_regex_name.clear();
                }
                break;
            case 'h':
                print_usage(argv);
                exit(0);
                break;
            case 't':
                data.title_regexes.push_back(pcrecpp::RE(optarg, pcrecpp::UTF8()));
                break;
        }

    if (dry_run) { // lets us print initialization options
        printf("simple_output = %i\n", output_type);
        exit(1);
    }

    // create a new instance of the expat parser
    XML_Parser parser = XML_ParserCreate("UTF-8");

    // initialize the elements of the struct to default values
    init_data(&data, output_type);


    // makes the parser pass "data" as the first argument to every callback 
    XML_SetUserData(parser, &data);
    void (*startFnPtr)(void*, const XML_Char*, const XML_Char**) = start;
    void (*endFnPtr)(void*, const XML_Char*) = end;
    void (*charHandlerFnPtr)(void*, const XML_Char*, int) = charhndl;

    // sets start and end to be the element start and end handlers
    XML_SetElementHandler(parser, startFnPtr, endFnPtr);
    // sets charhndl to be the callback for character data
    XML_SetCharacterDataHandler(parser, charHandlerFnPtr);

    bool done;
    char buf[BUFSIZ];

    // write header

    cout << "title" << "\t"
        << "articleid" << "\t"
        << "revid" << "\t"
        << "date" << "_"
        << "time" << "\t"
        << "anon" << "\t"
        << "editor" << "\t"
        << "editor_id" << "\t"
        << "minor" << "\t"
        << "text_size" << "\t"
        << "text_md5" << "\t"
        << "reversion" << "\t"
        << "additions_size" << "\t"
        << "deletions_size";

    int n = 0;
    if (!data.content_regexes.empty()) {
        for (vector<pcrecpp::RE>::iterator r = data.content_regexes.begin();
	     r != data.content_regexes.end(); ++r, ++n) {
            if (data.content_regex_names.at(n).empty()) {
	        cout << "\t" << "regex" << n;
            } else {
	        cout << "\t" << data.content_regex_names.at(n);
            }
        }
    }

    if (!data.diff_regexes.empty()) {
        for (vector<pcrecpp::RE>::iterator r = data.diff_regexes.begin(); r != data.diff_regexes.end(); ++r, ++n) {
            if (data.diff_regex_names.at(n).empty()) {
                cout << "\t" << "regex_" << n << "_add"
                     << "\t" << "regex_" << n << "_del";
            } else {
                cout << "\t" << data.diff_regex_names.at(n) << "_add"
                     << "\t" << data.diff_regex_names.at(n) << "_del";
            }
        }
    }

    cout << endl;
    
    // shovel data into the parser
    do {
        
        // read into buf a bufferfull of data from standard input
        size_t len = fread(buf, 1, BUFSIZ, stdin);
        done = len < BUFSIZ; // checks if we've got the last bufferfull
        
        // passes the buffer of data to the parser and checks for error
        //   (this is where the callbacks are invoked)
        if (XML_Parse(parser, buf, len, done) == XML_STATUS_ERROR) {
            cerr << "XML ERROR: " << XML_ErrorString(XML_GetErrorCode(parser)) << " at line "
                 << (int) XML_GetCurrentLineNumber(parser) << endl;
            return 1;
        }
    } while (!done);
   

    XML_ParserFree(parser);

    return 0;
}
