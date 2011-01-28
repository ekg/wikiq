/* 
 * An XML parser for Wikipedia Data dumps.
 * Converts XML files to tab-separated values files readable by spreadsheets
 * and statistical packages.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "expat.h"
#include <getopt.h>
#include "disorder.h"
#include "md5.h"

// timestamp of the form 2003-11-07T00:43:23Z
#define DATE_LENGTH 10
#define TIME_LENGTH 8
#define TIMESTAMP_LENGTH 20

#define MEGABYTE 1048576
#define FIELD_BUFFER_SIZE 1024
// 2048 KB in bytes + 1
//#define TEXT_BUFFER_SIZE 2097153
//#define TEXT_BUFFER_SIZE 10485760

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
}

void cleanup_revision(revisionData *data) {
    clean_data(data, 0);
}

void cleanup_article(revisionData *data) {
    clean_data(data, 1);
}


static void 
init_data(revisionData *data, outtype output_type)
{
    data->text = (char*) malloc(4 * MEGABYTE);  // 2MB is the article length limit, 4MB is 'safe'?
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

/* Write a header for the comma-separated output
 */
static void
write_header()
{
 //   printf("title, articleid, revid, date, time, anon, editor, editorid, minor, comment\n");
//    printf("title\tarticleid\trevid\tdate time\tanon\teditor\teditorid\tminor\n");

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

    // print line of tsv output
    printf("%s\t%s\t%s\t%s %s\t%s\t%s\t%s\t%s\t%i\t%f\t%s\n",
        data->title,
        data->articleid,
        data->revid,
        data->date,
        data->time,
        (data->editor[0] != '\0') ? "0" : "1",  // anon?
        data->editor,
        data->editorid,
        (data->minor) ? "1" : "0",
        (unsigned int) data->text_size,
        shannon_H(data->text, data->text_size),
        md5_hex_output
        );

    // 
    if (data->output_type == FULL) {
        printf("comment:%s\ntext:\n%s\n", data->comment, data->text);
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
   //size_t dest_len = strlen(dest);
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
    if (data->element != UNUSED && data->position != SKIP) {
        //char t[len];
        //strncpy(t,s,len);
        //t[len] = '\0'; // makes t a well-formed string
        switch (data->element) {
            case TEXT:
                   // printf("buffer length = %i, text: %s\n", len, t);
                    strlcatn(data->text, s, data->text_size, len);
                    data->text_size += len;
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
    fprintf(stderr, "usage: <wikimedia dump xml> | %s [options]\n", argv[0]);
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  -t   print text and comments after each line of tab separated data\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Takes a wikimedia data dump XML stream on standard in, and produces\n");
    fprintf(stderr, "a tab-separated stream of revisions on standard out:\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "title, articleid, revid, timestamp, anon, editor, editorid, minor, revlength, reventropy, revmd5\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "author: Erik Garrison <erik@hypervolu.me>\n");
}


int
main(int argc, char *argv[])
{
    
    enum outtype output_type;
    int dry_run = 0;
    // in "simple" output, we don't print text and comments
    output_type = SIMPLE;
    char c;

    while ((c = getopt(argc, argv, "ht")) != -1)
        switch (c)
        {
            case 'd':
                dry_run = 1;
                break;
            case 't':
                output_type = FULL;
                break;
            case 'h':
                print_usage(argv);
                exit(0);
                break;
        }

    if (dry_run) { // lets us print initialization options
        printf("simple_output = %i\n", output_type);
        exit(1);
    }

    // create a new instance of the expat parser
    XML_Parser parser = XML_ParserCreate("UTF-8");

    // initialize the user data struct which is passed to callback functions
    revisionData data;  
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
    
    // shovel data into the parser
    do {
        
        // read into buf a bufferfull of data from standard input
        size_t len = fread(buf, 1, BUFSIZ, stdin);
        done = len < BUFSIZ; // checks if we've got the last bufferfull
        
        // passes the buffer of data to the parser and checks for error
        //   (this is where the callbacks are invoked)
        if (XML_Parse(parser, buf, len, done) == XML_STATUS_ERROR) {
            fprintf(stderr,
                "%s at line %d\n",
                XML_ErrorString(XML_GetErrorCode(parser)),
                (int) XML_GetCurrentLineNumber(parser));
            return 1;
        }
    } while (!done);
   

    XML_ParserFree(parser);

    return 0;
}
