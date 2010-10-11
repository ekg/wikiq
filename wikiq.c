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

#define BUFFER_SIZE 80
// timestamp of the form 2003-11-07T00:43:23Z
#define DATE_LENGTH 10
#define TIME_LENGTH 8
#define TIMESTAMP_LENGTH 20

enum elements { 
    TITLE, ARTICLEID, REVISION, REVID, TIMESTAMP, CONTRIBUTOR, 
    EDITOR, EDITORID, MINOR, COMMENT, UNUSED, TEXT
}; 

enum block { TITLE_BLOCK, REVISION_BLOCK, CONTRIBUTOR_BLOCK, SKIP };

enum outtype { FULL, SIMPLE };

typedef struct {

    struct {
        char *title;
        char *articleid;
        char *revid;
        char *date;
        char *time;
        char *timestamp;
        char *anon;
        char *editor;
        char *editorid;
        bool minor;
        char *comment;
        char *text;
    } rev;
    
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
    if (title) {
        data->rev.title = NULL;
        data->rev.articleid = NULL;
    }
    data->rev.revid = NULL;
    data->rev.date = NULL;
    data->rev.time = NULL;
    data->rev.timestamp = NULL;
    data->rev.anon = NULL;
    data->rev.editor = NULL;
    data->rev.editorid = NULL;
    data->rev.minor = false;
    data->rev.comment = NULL; 
    data->rev.text = NULL;
    data->element = UNUSED;
    //data->position = 
}

static void
free_data(revisionData *data, int title)
{
    if (title) {
        //printf("freeing article\n");
        free(data->rev.title);
        free(data->rev.articleid);
    }
    free(data->rev.revid);
    free(data->rev.date);
    free(data->rev.time);
    free(data->rev.timestamp);
    free(data->rev.anon);
    free(data->rev.editor);
    free(data->rev.editorid);
    free(data->rev.comment);
    free(data->rev.text);
}

void cleanup_revision(revisionData *data) {
    free_data(data, 0);
    clean_data(data, 0);
}

void cleanup_article(revisionData *data) {
    free_data(data, 1);
    clean_data(data, 1);
}


static void 
init_data(revisionData *data, outtype output_type)
{
    clean_data(data, 1); // sets every element to null...
    data->output_type = output_type;
}

/* for debugging only, prints out the state of the data struct
 */
static void
print_state(revisionData *data) 
{
    printf("element = %i\n", data->element);
    printf("output_type = %i\n", data->output_type);
    printf("title = %s\n", data->rev.title);
    printf("articleid = %s\n", data->rev.articleid);
    printf("revid = %s\n", data->rev.revid);
    printf("date = %s\n", data->rev.date);
    printf("time = %s\n", data->rev.time);
    printf("anon = %s\n", data->rev.anon);
    printf("editor = %s\n", data->rev.editor);
    printf("editorid = %s\n", data->rev.editorid);
    printf("minor = %s\n", (data->rev.minor ? "1" : "0"));
    printf("comment = %s\n", data->rev.comment); 
    printf("text = %s\n", data->rev.text);
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

    // TODO: make it so you can specify fields to output
    // note that date and time are separated by a space, to match postgres's 
    // timestamp format
    printf("%s\t%s\t%s\t%s %s\t%s\t%s\t%s\t%s",
        (data->rev.title != NULL) ? data->rev.title : "",
        (data->rev.articleid != NULL) ? data->rev.articleid : "",
        (data->rev.revid != NULL) ? data->rev.revid : "",
        (data->rev.date != NULL) ? data->rev.date : "",
        (data->rev.time != NULL) ? data->rev.time : "",
        (data->rev.editor != NULL) ? "0" : "1",
        (data->rev.editor != NULL) ? data->rev.editor : "",
        (data->rev.editorid != NULL) ? data->rev.editorid  : "",
        (data->rev.minor) ? "1" : "0");
    switch (data->output_type)
    {
        case SIMPLE:
            printf("\n");
            break;
        case FULL:
            printf("\t%s\t%s\n",
                (data->rev.comment != NULL) ? data->rev.comment : "",
                (data->rev.text != NULL) ? data->rev.text : "");
            break;
    }

}

static char
*timestr(char *timestamp, char time_buffer[TIME_LENGTH+1])
{
    char *timeinstamp = &timestamp[DATE_LENGTH+1];
    strncpy(time_buffer, timeinstamp, TIME_LENGTH);
    time_buffer[TIME_LENGTH] = '\0'; // makes it a well-formed string
}


static char
*datestr(char *timestamp, char date_buffer[DATE_LENGTH+1])
{
    strncpy(date_buffer, timestamp, DATE_LENGTH);
    date_buffer[DATE_LENGTH] = '\0';
}

char
*append(char *entry, char *newstr)
{
    char *newbuff;
    int len;
    len = (strlen(entry)+strlen(newstr))*sizeof(char) + 1;
    newbuff = (char*) realloc(entry, len);
    strcat(newbuff, newstr);
    return newbuff;
}

char
*cache(char *entry, char *newstr)
{
    char *newbuff;
    int len;
    len = strlen(newstr)*sizeof(char) + 1; // include space for the '\0' !
    newbuff = (char*) malloc(len);
    strcpy(newbuff,newstr);
    return newbuff;

}

char
*store(char *entry, char *newstr)
{
    char *newbuff;
    if (entry == NULL)
        newbuff = cache(entry, newstr);
    else 
        newbuff = append(entry, newstr);
    return newbuff;
}

void
split_timestamp(revisionData *data) 
{
    char *t = data->rev.timestamp;
    char date_buffer[DATE_LENGTH+1];
    char time_buffer[TIME_LENGTH+1];
    datestr(t, date_buffer);
    timestr(t, time_buffer);
    data->rev.date = store(data->rev.date, date_buffer);
    data->rev.time = store(data->rev.time, time_buffer);
}

/* currently unused */
static int
is_whitespace(char *string) {
    int len = strlen(string);
    while (isspace(string[0]) && strlen(string) > 0) {
        string++;
    }
    if (strcmp(string, "") == 0)
        return 1;
    else
        return 0;
}

static void
squeeze(char *s, int c) {
    int i, j;
    for (i = j = 0; s[i] != '\0'; i++)
        if (s[i] != c)
            s[j++] = s[i];
    s[j] = '\0';
}

int
contains(char *s, char *t)
{
    char c = t[0]; //just get the first character of t
    int i = 0;
    while (s[i] != '\0') {
        if (s[i] == c) 
            return 1;
        i++;
    }
}

static void
charhndl(void* vdata, const XML_Char* s, int len)
{ 
    revisionData* data = (revisionData*) vdata;
    if (data->element != UNUSED && data->position != SKIP) {
        char t[len];
        strncpy(t,s,len);
        t[len] = '\0'; // makes t a well-formed string
        switch (data->element) {
            case TITLE:
                {
                    data->rev.title = store(data->rev.title, t);
                    // skip any articles with bad characters in their titles
                    break;
                }
            case ARTICLEID:
                   // printf("articleid = %s\n", t);
                    data->rev.articleid = store(data->rev.articleid, t);
                    break;
            case REVID:
                   // printf("revid = %s\n", t);
                    data->rev.revid = store(data->rev.revid, t);
                    break;
            case TIMESTAMP: 
                    data->rev.timestamp = store(data->rev.timestamp, t); 
                    if (strlen(data->rev.timestamp) == TIMESTAMP_LENGTH)
                        split_timestamp(data);
                    break;
            case EDITOR: {
                    data->rev.editor = store(data->rev.editor, t);
                    break;
                    }
            case EDITORID: 
                    //printf("editorid = %s\n", t);
                    data->rev.editorid = store(data->rev.editorid, t);
                    break;
            /* the following are implied or skipped:
            case MINOR: 
                    printf("found minor element\n");  doesn't work
                    break;                   minor tag is just a tag
            case UNUSED: 
            */
            case COMMENT: 
                   // printf("row: comment is %s\n", t);
                    if (data->output_type == FULL) {
                        data->rev.comment = store(data->rev.comment, t);
                    }
                    break;
            case TEXT:
                    if (data->output_type == FULL) {
                        data->rev.text = store(data->rev.text, t);
                    }
                   break; 
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
            data->rev.minor = true; 
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
    fprintf(stderr, "title, articleid, revid, date, time, anon, editor, editorid, minor\n");
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
    XML_Parser parser = XML_ParserCreate(NULL);

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
    // sets charhndl to be the callback for raw character data
    XML_SetCharacterDataHandler(parser, charHandlerFnPtr);

    int done;
    char buf[BUFSIZ];
    
    write_header();

    // shovel data into the parser
    do {
        
        // read into buf a bufferfull of data from standard input
        size_t len = fread(buf, 1, sizeof(buf), stdin);
        done = len < sizeof(buf); // checks if we've got the last bufferfull
        
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
