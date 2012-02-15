/*
  $Id$
  $URL$

  Copyright (c) 2006 - 2012
  Tilburg University

  A Tagger-Lemmatizer-Morphological-Analyzer-Dependency-Parser for Dutch
 
  This file is part of frog

  frog is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  frog is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  For questions and suggestions, see:
      http://ilk.uvt.nl/software.html
  or send mail to:
      timbl@uvt.nl
*/

#include "mbt/MbtAPI.h"
#include "libfolia/folia.h"
#include "frog/Frog.h"
#include "frog/iob_tagger_mod.h"

using namespace std;
using namespace folia;

IOBTagger::IOBTagger(){
  tagger = 0;
  iobLog = new LogStream( theErrLog, "iob-" );
}

IOBTagger::~IOBTagger(){
  delete tagger;
  delete iobLog;
}
 
bool IOBTagger::init( const Configuration& conf ){
  debug = tpDebug;
  string db = conf.lookUp( "debug", "IOB" );
  if ( !db.empty() )
    debug = folia::stringTo<int>( db );
  switch ( debug ){
  case 0:
  case 1:
    iobLog->setlevel(LogNormal);
    break;
  case 2:
  case 3:
  case 4:
    iobLog->setlevel(LogDebug);
    break;
  case 5:
  case 6:
  case 7:
    iobLog->setlevel(LogHeavy);
    break;
  default:
    iobLog->setlevel(LogExtreme);
  }    
  if (debug) 
    *Log(iobLog) << "IOB Chunker Init" << endl;
  if ( tagger != 0 ){
    *Log(iobLog) << "IOBTagger is already initialized!" << endl;
    return false;
  }  
  string tagset = conf.lookUp( "settings", "IOB" );
  if ( tagset.empty() ){
    *Log(iobLog) << "Unable to find settings for IOB" << endl;
    return false;
  }
  string init = "-s " + configuration.configDir() + tagset;
  if ( Tagger::Version() == "3.2.6" )
    init += " -vc";
  else
    init += " -vcf";
  tagger = new MbtAPI( init, *iobLog );
  return tagger != 0;
}

bool IOBTagger::splitOneWT( const string& inp, string& word, 
			    string& tag, string& confidence ){
  bool isKnown = true;
  string in = inp;
  //     split word and tag, and store num of slashes
  if (debug)
    *Log(iobLog) << "split Classify starting with " << in << endl;
  string::size_type pos = in.rfind("/");
  if ( pos == string::npos ) {
    *Log(iobLog) << "no word/tag/confidence triple in this line: " << in << endl;
    exit( EXIT_FAILURE );
  }
  else {
    confidence = in.substr( pos+1 );
    in.erase( pos );
  }
  pos = in.rfind("//");
  if ( pos != string::npos ) {
    // double slash: lets's hope is is an unknown word
    if ( pos == 0 ){
      // but this is definitely something like //LET() 
      word = "/";
      tag = in.substr(pos+2);
    }
    else {
      word = in.substr( 0, pos );
      tag = in.substr( pos+2 );
    }
    isKnown = false;
  } 
  else {
    pos = in.rfind("/");
    if ( pos != string::npos ) {
      word = in.substr( 0, pos );
      tag = in.substr( pos+1 );
    }
    else {
      *Log(iobLog) << "no word/tag/confidence triple in this line: " << in << endl;
      exit( EXIT_FAILURE );
    }
  }
  if ( debug){
    if ( isKnown )
      *Log(iobLog) << "known";
    else
      *Log(iobLog) << "unknown";
    *Log(iobLog) << " word: " << word << "\ttag: " << tag << "\tconfidence: " << confidence << endl;
  }
  return isKnown;
}

int IOBTagger::splitWT( const string& tagged,
			vector<string>& tags, 
			vector<double>& conf ){
  vector<string> words;
  vector<string> tagwords;
  vector<bool> known;
  tags.clear();
  conf.clear();
  size_t num_words = Timbl::split_at( tagged, tagwords, " " );
  num_words--; // the last "word" is <utt> which gets added by the tagger
  for( size_t i = 0; i < num_words; ++i ) {
    string word, tag, confs;
    bool isKnown = splitOneWT( tagwords[i], word, tag, confs );
    double confidence;
    if ( !stringTo<double>( confs, confidence ) ){
      *Log(iobLog) << "tagger confused. Expected a double, got '" << confs << "'" << endl;
      exit( EXIT_FAILURE );
    }
    words.push_back( word );
    tags.push_back( tag );
    known.push_back( isKnown );
    conf.push_back( confidence );
  }
  if (debug) {
    *Log(iobLog) << "#tagged_words: " << num_words << endl;
    for( size_t i = 0; i < num_words; i++) 
      *Log(iobLog)   << "\ttagged word[" << i <<"]: " << words[i] << (known[i]?"/":"//")
	     << tags[i] << " <" << conf[i] << ">" << endl;
  }
  return num_words;
}

void addChunk( FoliaElement *chunks, 
	       const vector<Word*>& words,
	       const vector<double>& confs,
	       const string& IOB ){
  double c = 1;
  for ( size_t i=0; i < confs.size(); ++i )
    c *= confs[i];
  FoliaElement *e = new Chunk("class='" + IOB +
			      "', confidence='" + toString(c) + "'");
#pragma omp critical(foliaupdate)
  {
    chunks->append( e );
  }
  for ( size_t i=0; i < words.size(); ++i ){
#pragma omp critical(foliaupdate)
    {
      e->append( words[i] );
    }
  }
}

void IOBTagger::addIOBTags( FoliaElement *sent, 
			    const vector<Word*>& words,
			    const vector<string>& tags,
			    const vector<double>& confs ){
  FoliaElement *el = 0;
  try {
    el = sent->annotation<ChunkingLayer>("iob" );
  }
  catch(...){
    el = new ChunkingLayer("iob");
#pragma omp critical(foliaupdate)
    {
      sent->append( el );
    }
  }
  vector<Word*> stack;
  vector<double> dstack;
  string curIOB;
  for ( size_t i=0; i < tags.size(); ++i ){
    if (debug) 
      *Log(iobLog) << "tag = " << tags[i] << endl;
    vector<string> tagwords;
    size_t num_words = Timbl::split_at( tags[i], tagwords, "_" );
    if ( num_words != 2 ){
      *Log(iobLog) << "expected <POS>_<IOB>, got: " << tags[i] << endl;
      exit( EXIT_FAILURE );
    }
    vector<string> iob;
    if (debug) 
      *Log(iobLog) << "IOB = " << tagwords[1] << endl;
    if ( tagwords[1] == "O" ){
      if ( !stack.empty() ){
	if (debug) {
	  *Log(iobLog) << "O spit out " << curIOB << endl;
	  using folia::operator<<;
	  *Log(iobLog) << "spit out " << stack << endl;	
	}
	addChunk( el, stack, dstack, curIOB );
	dstack.clear();
	stack.clear();
      }
      continue;
    }
    else {
      num_words = Timbl::split_at( tagwords[1], iob, "-" );
      if ( num_words != 2 ){
	*Log(iobLog) << "expected <IOB>-tag, got: " << tagwords[1] << endl;
	exit( EXIT_FAILURE );
      }
    }
    if ( iob[0] == "B" ||
	 ( iob[0] == "I" && stack.empty() ) ||
	 ( iob[0] == "I" && iob[1] != curIOB ) ){
      // an I without preceding B is handled as a B
      // an I with a different TAG is also handled as a B
      if ( !stack.empty() ){
	if ( debug ){
	  *Log(iobLog) << "B spit out " << curIOB << endl;
	  using folia::operator<<;
	  *Log(iobLog) << "spit out " << stack << endl;	
	}
	addChunk( el, stack, dstack, curIOB );
	dstack.clear();
	stack.clear();
      }
      curIOB = iob[1];
    }
    dstack.push_back( confs[i] );
    stack.push_back( words[i] );
  }
  if ( !stack.empty() ){
    if ( debug ){
      *Log(iobLog) << "END spit out " << curIOB << endl;
      using folia::operator<<;
      *Log(iobLog) << "spit out " << stack << endl;	
    }
    addChunk( el, stack, dstack, curIOB );
  }
}

string IOBTagger::Classify( FoliaElement *sent ){
  string tagged;
  vector<Word*> swords = sent->words();
  if ( !swords.empty() ) {
    vector<string> words;
    string sentence; // the tagger needs the whole sentence
    for ( size_t w = 0; w < swords.size(); ++w ) {
      sentence += swords[w]->str();
      words.push_back( swords[w]->str() );
      if ( w < swords.size()-1 )
	sentence += " ";
    }
    if (debug) 
      *Log(iobLog) << "IOB in: " << sentence << endl;
    tagged = tagger->Tag(sentence);
    if (debug) {
      *Log(iobLog) << "sentence: " << sentence << endl
		   << "IOB tagged: "<< tagged
		   << endl;
    }
    vector<double> conf;
    vector<string> tags;
    splitWT( tagged, tags, conf );
    addIOBTags( sent, swords, tags, conf );
  }
  return tagged;
}


