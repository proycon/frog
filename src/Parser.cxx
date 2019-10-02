/* ex: set tabstop=8 expandtab: */
/*
  Copyright (c) 2006 - 2019
  CLST  - Radboud University
  ILK   - Tilburg University

  This file is part of frog:

  A Tagger-Lemmatizer-Morphological-Analyzer-Dependency-Parser for
  several languages

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
      https://github.com/LanguageMachines/frog/issues
  or send mail to:
      lamasoftware (at ) science.ru.nl

*/

#include "frog/Parser.h"

#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
#include <algorithm>

#include "config.h"
#include "ticcutils/Configuration.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/SocketBasics.h"
#include "ticcutils/json.hpp"
#include "timbl/TimblAPI.h"
#include "frog/Frog-util.h"
#include "frog/csidp.h"

using namespace std;

using TiCC::operator<<;
using namespace nlohmann;

#define LOG *TiCC::Log(errLog)
#define DBG *TiCC::Dbg(dbgLog)

struct parseData {
  void clear() { words.clear(); heads.clear(); mods.clear(); mwus.clear(); }
  vector<string> words;
  vector<string> heads;
  vector<string> mods;
  vector<vector<folia::Word*> > mwus;
};

ostream& operator<<( ostream& os, const parseData& pd ){
  os << "pd words " << pd.words << endl;
  os << "pd heads " << pd.heads << endl;
  os << "pd mods " << pd.mods << endl;
  os << "pd mwus ";
  for ( const auto& mw : pd.mwus ){
    for ( const auto& w : mw ){
      os << w->id();
    }
    os << endl;
  }
  return os;
}

bool Parser::init( const TiCC::Configuration& configuration ){
  filter = 0;
  string pairsFileName;
  string pairsOptions = "-a1 +D -G0 +vdb+di";
  string dirFileName;
  string dirOptions = "-a1 +D -G0 +vdb+di";
  string relsFileName;
  string relsOptions = "-a1 +D -G0 +vdb+di";
  maxDepSpanS = "20";
  maxDepSpan = 20;
  bool problem = false;
  LOG << "initiating parser ... " << endl;
  string cDir = configuration.configDir();
  string val = configuration.lookUp( "debug", "parser" );
  if ( !val.empty() ){
    int level;
    if ( TiCC::stringTo<int>( val, level ) ){
      if ( level > 5 ){
	dbgLog->setlevel( LogLevel::LogDebug );
      }
    }
  }
  val = configuration.lookUp( "version", "parser" );
  if ( val.empty() ){
    _version = "1.0";
  }
  else {
    _version = val;
  }
  val = configuration.lookUp( "set", "parser" );
  if ( val.empty() ){
    dep_tagset = "http://ilk.uvt.nl/folia/sets/frog-depparse-nl";
  }
  else {
    dep_tagset = val;
  }
  val = configuration.lookUp( "set", "tagger" );
  if ( val.empty() ){
    POS_tagset = "http://ilk.uvt.nl/folia/sets/frog-mbpos-cgn";
  }
  else {
    POS_tagset = val;
  }
  val = configuration.lookUp( "set", "mwu" );
  if ( val.empty() ){
    MWU_tagset = "http://ilk.uvt.nl/folia/sets/frog-mwu-nl";
  }
  else {
    MWU_tagset = val;
  }
  string charFile = configuration.lookUp( "char_filter_file", "tagger" );
  if ( charFile.empty() )
    charFile = configuration.lookUp( "char_filter_file" );
  if ( !charFile.empty() ){
    charFile = prefix( configuration.configDir(), charFile );
    filter = new TiCC::UniFilter();
    filter->fill( charFile );
  }
  val = configuration.lookUp( "maxDepSpan", "parser" );
  if ( !val.empty() ){
    size_t gs = TiCC::stringTo<size_t>( val );
    if ( gs < 50 ){
      maxDepSpanS = val;
      maxDepSpan = gs;
    }
    else {
      LOG << "invalid maxDepSpan value in config file" << endl;
      LOG << "keeping default " << maxDepSpan << endl;
      problem = true;
    }
  }

  val = configuration.lookUp( "host", "parser" );
  if ( !val.empty() ){
    // so using an external parser server
    _host = val;
    val = configuration.lookUp( "port", "parser" );
    if ( !val.empty() ){
      _port = val;
    }
    else {
      LOG << "missing port option" << endl;
      problem = true;
    }
    val = configuration.lookUp( "pairs_base", "parser" );
    if ( !val.empty() ){
      _pairs_base = val;
    }
    else {
      LOG << "missing pairs_base option" << endl;
      problem = true;
    }
    val = configuration.lookUp( "dirs_base", "parser" );
    if ( !val.empty() ){
      _dirs_base = val;
    }
    else {
      LOG << "missing dirs_base option" << endl;
      problem = true;
    }
    val = configuration.lookUp( "rels_base", "parser" );
    if ( !val.empty() ){
      _rels_base = val;
    }
    else {
      LOG << "missing rels_base option" << endl;
      problem = true;
    }
  }
  else {
    val = configuration.lookUp( "pairsFile", "parser" );
    if ( !val.empty() ){
      pairsFileName = prefix( cDir, val );
    }
    else {
      LOG << "missing pairsFile option" << endl;
      problem = true;
    }
    val = configuration.lookUp( "pairsOptions", "parser" );
    if ( !val.empty() ){
      pairsOptions = val;
    }
    val = configuration.lookUp( "dirFile", "parser" );
    if ( !val.empty() ){
      dirFileName = prefix( cDir, val );
    }
    else {
      LOG << "missing dirFile option" << endl;
      problem = true;
    }
    val = configuration.lookUp( "dirOptions", "parser" );
    if ( !val.empty() ){
      dirOptions = val;
    }
    val = configuration.lookUp( "relsFile", "parser" );
    if ( !val.empty() ){
      relsFileName = prefix( cDir, val );
    }
    else {
      LOG << "missing relsFile option" << endl;
      problem = true;
    }
    val = configuration.lookUp( "relsOptions", "parser" );
    if ( !val.empty() ){
      relsOptions = val;
    }
  }
  if ( problem ) {
    return false;
  }

  string cls = configuration.lookUp( "outputclass" );
  if ( !cls.empty() ){
    textclass = cls;
  }
  else {
    textclass = "current";
  }
  bool happy = true;
  if ( _host.empty() ){
    pairs = new Timbl::TimblAPI( pairsOptions );
    if ( pairs->Valid() ){
      LOG << "reading " <<  pairsFileName << endl;
      happy = pairs->GetInstanceBase( pairsFileName );
    }
    else {
      LOG << "creating Timbl for pairs failed:"
	  << pairsOptions << endl;
      happy = false;
    }
    if ( happy ){
      dir = new Timbl::TimblAPI( dirOptions );
      if ( dir->Valid() ){
	LOG << "reading " <<  dirFileName << endl;
	happy = dir->GetInstanceBase( dirFileName );
      }
      else {
	LOG << "creating Timbl for dir failed:" << dirOptions << endl;
	happy = false;
      }
      if ( happy ){
	rels = new Timbl::TimblAPI( relsOptions );
	if ( rels->Valid() ){
	  LOG << "reading " <<  relsFileName << endl;
	  happy = rels->GetInstanceBase( relsFileName );
	}
	else {
	  LOG << "creating Timbl for rels failed:"
	      << relsOptions << endl;
	  happy = false;
	}
      }
    }
  }
  else {
    LOG << "using Parser Timbl's on " << _host << ":" << _port << endl;
  }
  isInit = happy;
  return happy;
}

Parser::~Parser(){
  delete rels;
  delete dir;
  delete pairs;
  delete errLog;
  delete dbgLog;
  delete filter;
}

vector<string> Parser::createPairInstances( const parseData& pd ){
  vector<string> instances;
  const vector<string>& words = pd.words;
  const vector<string>& heads = pd.heads;
  const vector<string>& mods = pd.mods;
  if ( words.size() == 1 ){
    string inst =
      "__ " + words[0] + " __ ROOT ROOT ROOT __ " + heads[0]
      + " __ ROOT ROOT ROOT "+ words[0] +"^ROOT ROOT ROOT ROOT^"
      + heads[0] + " _";
    instances.push_back( inst );
  }
  else {
    for ( size_t i=0 ; i < words.size(); ++i ){
      string word_1, word0, word1;
      string tag_1, tag0, tag1;
      string mods0;
      if ( i == 0 ){
	word_1 = "__";
	tag_1 = "__";
      }
      else {
	word_1 = words[i-1];
	tag_1 = heads[i-1];
      }
      word0 = words[i];
      tag0 = heads[i];
      mods0 = mods[i];
      if ( i == words.size() - 1 ){
	word1 = "__";
	tag1 = "__";
      }
      else {
	word1 = words[i+1];
	tag1 = heads[i+1];
      }
      string inst = word_1 + " " + word0 + " " + word1
	+ " ROOT ROOT ROOT " + tag_1 + " "
	+ tag0 + " " + tag1 + " ROOT ROOT ROOT " + tag0
	+ "^ROOT ROOT ROOT ROOT^" + mods0 + " _";
      instances.push_back( inst );
    }
    //
    for ( size_t wPos=0; wPos < words.size(); ++wPos ){
      string w_word_1, w_word0, w_word1;
      string w_tag_1, w_tag0, w_tag1;
      string w_mods0;
      if ( wPos == 0 ){
	w_word_1 = "__";
	w_tag_1 = "__";
      }
      else {
	w_word_1 = words[wPos-1];
	w_tag_1 = heads[wPos-1];
      }
      w_word0 = words[wPos];
      w_tag0 = heads[wPos];
      w_mods0 = mods[wPos];
      if ( wPos == words.size()-1 ){
	w_word1 = "__";
	w_tag1 = "__";
      }
      else {
	w_word1 = words[wPos+1];
	w_tag1 = heads[wPos+1];
      }
      for ( size_t pos=0; pos < words.size(); ++pos ){
	if ( pos > wPos + maxDepSpan ){
	  break;
	}
	if ( pos == wPos ){
	  continue;
	}
	if ( pos + maxDepSpan < wPos ){
	  continue;
	}
	string inst = w_word_1 + " " + w_word0 + " " + w_word1;

	if ( pos == 0 ){
	  inst += " __";
	}
	else {
	  inst += " " + words[pos-1];
	}
	if ( pos < words.size() ){
	  inst += " " + words[pos];
	}
	else {
	  inst += " __";
	}
	if ( pos < words.size()-1 ){
	  inst += " " + words[pos+1];
	}
	else {
	  inst += " __";
	}
	inst += " " + w_tag_1 + " " + w_tag0 + " " + w_tag1;
	if ( pos == 0 ){
	  inst += " __";
	}
	else {
	  inst += " " + heads[pos-1];
	}
	if ( pos < words.size() ){
	  inst += " " + heads[pos];
	}
	else {
	  inst += " __";
	}
	if ( pos < words.size()-1 ){
	  inst += " " + heads[pos+1];
	}
	else {
	  inst += " __";
	}

	inst += " " + w_tag0 + "^";
	if ( pos < words.size() ){
	  inst += heads[pos];
	}
	else {
	  inst += "__";
	}

	if ( wPos > pos ){
	  inst += " LEFT " + TiCC::toString( wPos - pos );
	}
	else {
	  inst += " RIGHT "+ TiCC::toString( pos - wPos );
	}
	if ( pos >= words.size() ){
	  inst += " __";
	}
	else {
	  inst += " " + mods[pos];
	}
	inst += "^" + w_mods0 + " __";
	instances.push_back( inst );
      }
    }
  }
  return instances;
}

vector<string> Parser::createDirInstances( const parseData& pd ){
  vector<string> d_instances;
  const vector<string>& words = pd.words;
  const vector<string>& heads = pd.heads;
  const vector<string>& mods = pd.mods;

  if ( words.size() == 1 ){
    string word0 = words[0];
    string tag0 = heads[0];
    string mod0 = mods[0];
    string inst = "__ __ " + word0 + " __ __ __ __ " + tag0
      + " __ __ __ __ " + word0 + "^" + tag0
      + " __ __ __^" + tag0 + " " + tag0 +"^__ __ " + mod0
      + " __ ROOT";
    d_instances.push_back( inst );
  }
  else if ( words.size() == 2 ){
    string word0 = words[0];
    string tag0 = heads[0];
    string mod0 = mods[0];
    string word1 = words[1];
    string tag1 = heads[1];
    string mod1 = mods[1];
    string inst = string("__ __")
      + " " + word0
      + " " + word1
      + " __ __ __"
      + " " + tag0
      + " " + tag1
      + " __ __ __"
      + " " + word0 + "^" + tag0
      + " " + word1 + "^" + tag1
      + " __ __^" + tag0
      + " " + tag0 + "^" + tag1
      + " __"
      + " " + mod0
      + " " + mod1
      + " ROOT";
    d_instances.push_back( inst );
    inst = string("__")
      + " " + word0
      + " " + word1
      + " __ __ __"
      + " " + tag0
      + " " + tag1
      + " __ __ __"
      + " " + word0 + "^" + tag0
      + " " + word1 + "^" + tag1
      + " __ __"
      + " " + tag0 + "^" + tag1
      + " " + tag1 + "^__"
      + " " + mod0
      + " " + mod1
      + " __"
      + " ROOT";
    d_instances.push_back( inst );
  }
  else if ( words.size() == 3 ) {
    string word0 = words[0];
    string tag0 = heads[0];
    string mod0 = mods[0];
    string word1 = words[1];
    string tag1 = heads[1];
    string mod1 = mods[1];
    string word2 = words[2];
    string tag2 = heads[2];
    string mod2 = mods[2];
    string inst = string("__ __")
      + " " + word0
      + " " + word1
      + " " + word2
      + " __ __"
      + " " + tag0
      + " " + tag1
      + " " + tag2
      + " __ __"
      + " " + word0 + "^" + tag0
      + " " + word1 + "^" + tag1
      + " " + word2 + "^" + tag2
      + " __^" + tag0
      + " " + tag0 + "^" + tag1
      + " __"
      + " " + mod0
      + " " + mod1
      + " ROOT";
    d_instances.push_back( inst );
    inst = string("__")
      + " " + word0
      + " " + word1
      + " " + word2
      + " __ __"
      + " " + tag0
      + " " + tag1
      + " " + tag2
      + " __ __"
      + " " + word0 + "^" + tag0
      + " " + word1 + "^" + tag1
      + " " + word2 + "^" + tag2
      + " __"
      + " " + tag0 + "^" + tag1
      + " " + tag1 + "^" + tag2
      + " " + mod0
      + " " + mod1
      + " " + mod2
      + " ROOT";
    d_instances.push_back( inst );
    inst = word0
      + " " + word1
      + " " + word2
      + " __ __"
      + " " + tag0
      + " " + tag1
      + " " + tag2
      + " __ __"
      + " " + word0 + "^" + tag0
      + " " + word1 + "^" + tag1
      + " " + word2 + "^" + tag2
      + " __ __"
      + " " + tag1 + "^" + tag2
      + " " + tag2 + "^__"
      + " " + mod1
      + " " + mod2
      + " __"
      + " ROOT";
    d_instances.push_back( inst );
  }
  else {
    for ( size_t i=0 ; i < words.size(); ++i ){
      string word_1, word_2;
      string tag_1, tag_2;
      string mod_1;
      if ( i == 0 ){
	word_2 = "__";
	tag_2 = "__";
	//	mod_2 = "__";
	word_1 = "__";
	tag_1 = "__";
	mod_1 = "__";
      }
      else if ( i == 1 ){
	word_2 = "__";
	tag_2 = "__";
	//	mod_2 = "__";
	word_1 = words[i-1];
	tag_1 = heads[i-1];
	mod_1 = mods[i-1];
      }
      else {
	word_2 = words[i-2];
	tag_2 = heads[i-2];
	//	mod_2 = mods[i-2];
	word_1 = words[i-1];
	tag_1 = heads[i-1];
	mod_1 = mods[i-1];
      }
      string word0 = words[i];
      string word1, word2;
      string tag0 = heads[i];
      string tag1, tag2;
      string mod0 = mods[i];
      string mod1;
      if ( i < words.size() - 2 ){
	word1 = words[i+1];
	tag1 = heads[i+1];
	mod1 = mods[i+1];
	word2 = words[i+2];
	tag2 = heads[i+2];
	//	mod2 = mods[i+2];
      }
      else if ( i == words.size() - 2 ){
	word1 = words[i+1];
	tag1 = heads[i+1];
	mod1 = mods[i+1];
	word2 = "__";
	tag2 = "__";
	//	mod2 = "__";
      }
      else {
	word1 = "__";
	tag1 = "__";
	mod1 = "__";
	word2 = "__";
	tag2 = "__";
	//	mod2 = "__";
      }
      string inst = word_2
	+ " " + word_1
	+ " " + word0
	+ " " + word1
	+ " " + word2
	+ " " + tag_2
	+ " " + tag_1
	+ " " + tag0
	+ " " + tag1
	+ " " + tag2
	+ " " + word_2 + "^" + tag_2
	+ " " + word_1 + "^" + tag_1
	+ " " + word0 + "^" + tag0
	+ " " + word1 + "^" + tag1
	+ " " + word2 + "^" + tag2
	+ " " + tag_1 + "^" + tag0
	+ " " + tag0 + "^" + tag1
	+ " " + mod_1
	+ " " + mod0
	+ " " + mod1
	+ " ROOT";
      d_instances.push_back( inst );
    }
  }
  return d_instances;
}

vector<string> Parser::createRelInstances( const parseData& pd ){
  vector<string> r_instances;
  const vector<string>& words = pd.words;
  const vector<string>& heads = pd.heads;
  const vector<string>& mods = pd.mods;

  if ( words.size() == 1 ){
    string word0 = words[0];
    string tag0 = heads[0];
    string mod0 = mods[0];
    string inst = "__ __ " + word0 + " __ __ " + mod0
      + " __ __ "  + tag0 + " __ __ __^" + tag0
      + " " + tag0 + "^__ __^__^" + tag0
      + " " + tag0 + "^__^__ __";
    r_instances.push_back( inst );
  }
  else if ( words.size() == 2 ){
    string word0 = words[0];
    string tag0 = heads[0];
    string mod0 = mods[0];
    string word1 = words[1];
    string tag1 = heads[1];
    string mod1 = mods[1];
    //
    string inst = string("__ __")
      + " " + word0
      + " " + word1
      + " __"
      + " " + mod0
      + " __ __"
      + " " + tag0
      + " " + tag1
      + " __"
      + " __^" + tag0
      + " " + tag0 + "^" + tag1
      + " __^__^" + tag0
      + " " + tag0 + "^" + tag1 + "^__"
      + " __";
    r_instances.push_back( inst );
    inst = string("__")
      + " " + word0
      + " " + word1
      + " __ __"
      + " " + mod1
      + " __"
      + " " + tag0
      + " " + tag1
      + " __ __"
      + " " + tag0 + "^" + tag1
      + " " + tag1 + "^__"
      + " __^" + tag0 + "^" + tag1
      + " " + tag1 + "^__^__"
      + " __";
    r_instances.push_back( inst );
  }
  else if ( words.size() == 3 ) {
    string word0 = words[0];
    string tag0 = heads[0];
    string mod0 = mods[0];
    string word1 = words[1];
    string tag1 = heads[1];
    string mod1 = mods[1];
    string word2 = words[2];
    string tag2 = heads[2];
    string mod2 = mods[2];
    //
    string inst = string("__ __")
      + " " + word0
      + " " + word1
      + " " + word2
      + " " + mod0
      + " __ __"
      + " " + tag0
      + " " + tag1
      + " " + tag2
      + " __^" + tag0
      + " " + tag0 + "^" + tag1
      + " __^__^" + tag0
      + " " + tag0 + "^" + tag1 + "^" + tag2
      + " __";
    r_instances.push_back( inst );
    inst = string("__")
      + " " + word0
      + " " + word1
      + " " + word2
      + " __"
      + " " + mod1
      + " __"
      + " " + tag0
      + " " + tag1
      + " " + tag2
      + " __"
      + " " + tag0 + "^" + tag1
      + " " + tag1 + "^" + tag2
      + " __^" + tag0 + "^" + tag1
      + " " + tag1 + "^" + tag2 + "^__"
      + " __";
    r_instances.push_back( inst );
    inst = word0
      + " " + word1
      + " " + word2
      + " __ __"
      + " " + mod2
      + " " + tag0
      + " " + tag1
      + " " + tag2
      + " __ __"
      + " " + tag1 + "^" + tag2
      + " " + tag2 + "^__"
      + " " + tag0 + "^" + tag1 + "^" + tag2
      + " " + tag2 + "^__^__"
      + " __";
    r_instances.push_back( inst );
  }
  else {
    for ( size_t i=0 ; i < words.size(); ++i ){
      string word_1, word_2;
      string tag_1, tag_2;
      if ( i == 0 ){
	word_2 = "__";
	tag_2 = "__";
	word_1 = "__";
	tag_1 = "__";
      }
      else if ( i == 1 ){
	word_2 = "__";
	tag_2 = "__";
	word_1 = words[i-1];
	tag_1 = heads[i-1];
      }
      else {
	word_2 = words[i-2];
	tag_2 = heads[i-2];
	word_1 = words[i-1];
	tag_1 = heads[i-1];
      }
      string word0 = words[i];
      string word1, word2;
      string tag0 = heads[i];
      string tag1, tag2;
      string mod0 = mods[i];
      if ( i < words.size() - 2 ){
	word1 = words[i+1];
	tag1 = heads[i+1];
	word2 = words[i+2];
	tag2 = heads[i+2];
      }
      else if ( i == words.size() - 2 ){
	word1 = words[i+1];
	tag1 = heads[i+1];
	word2 = "__";
	tag2 = "__";
      }
      else {
	word1 = "__";
	tag1 = "__";
	word2 = "__";
	tag2 = "__";
      }
      //
      string inst = word_2
	+ " " + word_1
	+ " " + word0
	+ " " + word1
	+ " " + word2
	+ " " + mod0
	+ " " + tag_2
	+ " " + tag_1
	+ " " + tag0
	+ " " + tag1
	+ " " + tag2
	+ " " + tag_1 + "^" + tag0
	+ " " + tag0 + "^" + tag1
	+ " " + tag_2 + "^" + tag_1 + "^" + tag0
	+ " " + tag0 + "^" + tag1 + "^" + tag2
	+ " __";
      r_instances.push_back( inst );
    }
  }
  return r_instances;
}


void Parser::add_provenance( folia::Document& doc, folia::processor *main ) const {
  string _label = "dep-parser";
  if ( !main ){
    throw logic_error( "Parser::add_provenance() without arguments." );
  }
  folia::KWargs args;
  args["name"] = _label;
  args["generate_id"] = "auto()";
  args["version"] = _version;
  args["begindatetime"] = "now()";
  folia::processor *proc = doc.add_processor( args, main );
  args.clear();
  args["processor"] = proc->id();
  doc.declare( folia::AnnotationType::DEPENDENCY, dep_tagset, args );
}

void extract( const string& tv, string& head, string& mods ){
  vector<string> v = TiCC::split_at_first_of( tv, "()" );
  head = v[0];
  mods.clear();
  if ( v.size() > 1 ){
    vector<string> mv = TiCC::split_at( v[1], "," );
    mods = mv[0];
    for ( size_t i=1; i < mv.size(); ++i ){
      mods += "|" + mv[i];
    }
  }
  else {
    mods = ""; // HACK should be "__", but then there are differences!
  }                                                      //     ^
}                                                        //     |
                                                         //     |
parseData Parser::prepareParse( frog_data& fd ){         //     |
  parseData pd;                                          //     |
  for ( size_t i = 0; i < fd.units.size(); ++i ){        //     |
    if ( fd.mwus.find( i ) == fd.mwus.end() ){           //     |
      string head;                                       //     |
      string mods;                                       //     |
      extract( fd.units[i].tag, head, mods );            //     |
      string word_s = fd.units[i].word;                  //     |
      // the word may contain spaces, remove them all!          |
      word_s.erase(remove_if(word_s.begin(), word_s.end(), ::isspace), word_s.end());
      pd.words.push_back( word_s );                      //     |
      pd.heads.push_back( head );                        //     |
      if ( mods.empty() ){                               //    \/
	// HACK: make this bug-to-bug compatible with older versions.
	// But in fact this should also be done for the mwu's loop below!
	// now sometimes empty mods get appended there.
	// extract should return "__", I suppose  (see above)
	mods = "__";
      }
      pd.mods.push_back( mods );
    }
    else {
      string multi_word;
      string multi_head;
      string multi_mods;
      for ( size_t k = i; k <= fd.mwus[i]; ++k ){
	icu::UnicodeString tmp = TiCC::UnicodeFromUTF8( fd.units[k].word );
	if ( filter ){
	  tmp = filter->filter( tmp );
	}
	string ms = TiCC::UnicodeToUTF8( tmp );
	// the word may contain spaces, remove them all!
	ms.erase(remove_if(ms.begin(), ms.end(), ::isspace), ms.end());
	string head;
	string mods;
	extract( fd.units[k].tag, head, mods );
	if ( k == i ){
	  multi_word = ms;
	  multi_head = head;
	  multi_mods = mods;
	}
	else {
	  multi_word += "_" + ms;
	  multi_head += "_" + head;
	  multi_mods += "_" + mods;
	}
      }
      pd.words.push_back( multi_word );
      pd.heads.push_back( multi_head );
      pd.mods.push_back( multi_mods );
      i = fd.mwus[i];
    }
  }
  return pd;
}


void timbl( Timbl::TimblAPI* tim,
	    const vector<string>& instances,
	    vector<timbl_result>& results ){
  results.clear();
  for ( const auto& inst : instances ){
    const Timbl::ValueDistribution *db;
    const Timbl::TargetValue *tv = tim->Classify( inst, db );
    results.push_back( timbl_result( tv->Name(), db->Confidence(tv), db ) );
  }
}

vector<pair<string,double>> parse_vd( const string& ds ){
  vector<pair<string,double>> result;
  vector<string> parts = TiCC::split_at_first_of( ds, "{,}" );
  for ( const auto& p : parts ){
    vector<string> sd = TiCC::split( p );
    assert( sd.size() == 2 );
    result.push_back( make_pair( sd[0], TiCC::stringTo<double>( sd[1] ) ) );
  }
  return result;
}

void Parser::timbl_server( const string& base,
			   const vector<string>& instances,
			   vector<timbl_result>& results ){
  results.clear();

  Sockets::ClientSocket client;
  if ( !client.connect( _host, _port ) ){
    LOG << "failed to open connection, " << _host
	<< ":" << _port << endl
	<< "Reason: " << client.getMessage() << endl;
    exit( EXIT_FAILURE );
  }
  DBG << "calling " << base << " server" << endl;
  string line;
  client.read( line );
  json response;
  try {
    response = json::parse( line );
  }
  catch ( const exception& e ){
    LOG << "json parsing failed on '" << line << "':"
	<< e.what() << endl;
    abort();
  }
  DBG << "received json data:" << response.dump(2) << endl;

  json out_json;
  out_json["command"] = "base";
  out_json["param"] = base;
  string out_line = out_json.dump() + "\n";
  DBG << "sending BASE json data:" << out_line << endl;
  client.write( out_line );
  client.read( line );
  try {
    response = json::parse( line );
  }
  catch ( const exception& e ){
    LOG << "json parsing failed on '" << line << "':"
	<< e.what() << endl;
    abort();
  }
  DBG << "received json data:" << response.dump(2) << endl;

  // create json query struct
  json query;
  query["command"] = "classify";
  json arr = json::array();
  for ( const auto& i : instances ){
    arr.push_back( i );
  }
  query["params"] = arr;
  DBG << "send json" << query.dump(2) << endl;
  // send it to the server
  line = query.dump() + "\n";
  client.write( line );
  // receive json
  client.read( line );
  DBG << "received line:" << line << "" << endl;
  try {
    response = json::parse( line );
  }
  catch ( const exception& e ){
    LOG << "json parsing failed on '" << line << "':"
	<< e.what() << endl;
    LOG << "the request to the server was: '" <<  query.dump(2) << "'" << endl;
    abort();
  }
  DBG << "received json data:" << response.dump(2) << endl;
  if ( !response.is_array() ){
    string cat = response["category"];
    double conf = response.value("confidence",0.0);
    vector<pair<string,double>> vd = parse_vd(response["distribution"]);
    results.push_back( timbl_result( cat, conf, vd ) );
  }
  else {
    for ( const auto& it : response.items() ){
      string cat = it.value()["category"];
      double conf = it.value().value("confidence",0.0);
      vector<pair<string,double>> vd = parse_vd( it.value()["distribution"] );
      results.push_back( timbl_result( cat, conf, vd ) );
    }
  }
}

void appendParseResult( frog_data& fd,
			const vector<parsrel>& res ){
  vector<int> nums;
  vector<string> roles;
  for ( const auto& it : res ){
    nums.push_back( it.head );
    roles.push_back( it.deprel );
  }
  for ( size_t i = 0; i < nums.size(); ++i ){
    fd.mw_units[i].parse_index = nums[i];
    fd.mw_units[i].parse_role = roles[i];
  }
}

//#define TEST_ALPINO_SERVER
#ifdef TEST_ALPINO_SERVER

xmlDoc *alpino_server_parse( frog_data& fd ){
  string host = "localhost"; // config.lookUp( "host", "alpino" );
  string port = "8004"; // config.lookUp( "port", "alpino" );
  Sockets::ClientSocket client;
  if ( !client.connect( host, port ) ){
    cerr << "failed to open Alpino connection: "<< host << ":" << port << endl;
    cerr << "Reason: " << client.getMessage() << endl;
    exit( EXIT_FAILURE );
  }
  //  cerr << "start input loop" << endl;
  string txt = fd.sentence();
  client.write( txt + "\n\n" );
  string result;
  string s;
  while ( client.read(s) ){
    result += s + "\n";
  }
#ifdef DEBUG_ALPINO
  cerr << "received data [" << result << "]" << endl;
#endif
  xmlDoc *doc = xmlReadMemory( result.c_str(), result.length(),
			       0, 0, XML_PARSE_NOBLANKS );
  return doc;
}

struct dp_tree {
  int id;
  int begin;
  int end;
  int word_index;
  string word;
  string rel;
  dp_tree *link;
  dp_tree *next;
  ~dp_tree(){ delete link; delete next; };
};

ostream& operator<<( ostream& os, const dp_tree *node ){
  if ( node ){
    os << node->rel << "[" << node->begin << "," << node->end << "] ("
       << node->word << ")";
    if ( node->word_index > 0 ){
      os << "#" << node->word_index;
    }
  }
  else {
    os << "null";
  }
  return os;
}

void print_nodes( int indent, const dp_tree *store ){
  const dp_tree *pnt = store;
  while ( pnt ){
    cerr << std::string(indent, ' ') << pnt << endl;
    print_nodes( indent+3, pnt->link );
    pnt = pnt->next;
  }
}

const dp_tree *extract_hd( const dp_tree *node ){
  const dp_tree *pnt = node->link;
  while ( pnt ){
    if ( pnt->rel == "hd" ){
      return pnt;
    }
    else if ( pnt->rel == "crd" ){
      return pnt;
    }
    pnt = pnt->next;
  }
  return 0;
}

int mwu_size( const dp_tree *node ){
  int result = 0;
  dp_tree *pnt = node->link;
  while ( pnt ){
    if ( pnt->rel == "mwp" ){
      ++result;
    }
    pnt = pnt->next;
  }
  return result;
}

void extract_dp( const dp_tree *store, const dp_tree *root,
		 const string& h,
		 vector<pair<string,int>>& result ){
  const dp_tree *pnt = store;
  //  cerr << "LOOP over: " << pnt << endl;
  while ( pnt ){
    //    cerr << "\tLOOP: " << pnt->rel << endl;
    if ( pnt->begin+1 == pnt->end ){
      if ( pnt == root ){
	if ( root->rel == "--"  ){
	  //	  cerr << "THAT A ";
	  //	  cerr << "0\tROOT" << endl;
	  result.push_back( make_pair("ROOT",0) );
	}
	else if ( h == "--" ){
	  //	  cerr << "THAT B ";
	  //	  cerr << "0\tROOT" << endl;
	  result.push_back( make_pair("ROOT",0) );
	}
	else {
	  //	  cerr << "THAT C ";
	  //	  cerr << store->begin << "\t" << h << endl;
	  result.push_back( make_pair(h,store->begin ) );
	}
      }
      else if ( root && root->end > 0 ){
	//	cerr << "THOSE A ";
	result.push_back( make_pair(pnt->rel,root->end ) );
	//	cerr << root->end << "\t" << pnt->rel << endl;
      }
      else if ( pnt->rel == "--" ){
	//	cerr << "THOSE B ";
	result.push_back( make_pair("punct",pnt->begin ) );
	//	cerr << pnt->begin << "\tpunct" << endl;
      }
      else {
	//	cerr << "THOSE C ";
	result.push_back( make_pair(pnt->rel, pnt->begin ) );
	//	cerr << pnt->begin << "\t" << pnt->rel << endl;
      }
    }
    else {
      if ( pnt->link ){
	// if ( root ){
	//   cerr << "DADA " << root->end << "\t" << pnt->rel << endl;
	// }
	const dp_tree *new_root = extract_hd( pnt );
	// if ( new_root ){
	//   cerr << pnt->rel << ", root at this level: " << new_root << endl;
	// }
	extract_dp( pnt->link, new_root, pnt->rel, result );
      }
    }
    pnt = pnt->next;
  }
}

dp_tree *parse_node( xmlNode *node, int& index ){
  auto atts = TiCC::getAttributes( node );
  //  cerr << "attributes: " << atts << endl;
  dp_tree *dp = new dp_tree();
  dp->id = TiCC::stringTo<int>( atts["id"] );
  dp->begin = TiCC::stringTo<int>( atts["begin"] );
  dp->end = TiCC::stringTo<int>( atts["end"] );
  dp->rel = atts["rel"];
  dp->word = atts["word"];
  if ( !dp->word.empty() ){
    dp->word_index = ++index;
  }
  dp->link = 0;
  dp->next = 0;
  return dp;
}

dp_tree *parse_nodes( xmlNode *node, int& index ){
  dp_tree *result = 0;
  xmlNode *pnt = node;
  dp_tree *last = 0;
  while ( pnt ){
    if ( TiCC::Name( pnt ) == "node" ){
      dp_tree *parsed = parse_node( pnt, index );
      // cerr << "parsed ";
      // print_node( parsed );
      if ( result == 0 ){
	result = parsed;
	last = result;
      }
      else {
	last->next = parsed;
	last = last->next;
      }
      if ( pnt->children ){
	dp_tree *childs = parse_nodes( pnt->children, index );
	last->link = childs;
      }
    }
    pnt = pnt->next;
  }
  return result;
}

dp_tree *resolve_mwus( dp_tree *in,
		       int& new_index,
		       int& compensate ){
  dp_tree *result = in;
  dp_tree *pnt = in;
  while ( pnt ){
    if ( pnt->link && pnt->link->rel == "mwp" ){
      dp_tree *tmp = pnt->link;
      pnt->word = tmp->word;
      pnt->word_index = tmp->word_index;
      int count = 0;
      tmp = tmp->next;
      while ( tmp ){
	++count;
	pnt->word += "_" + tmp->word;
	tmp = tmp->next;
      }
      tmp = pnt->link;
      pnt->link = 0;
      delete tmp;
      ++new_index;
      pnt->end = pnt->begin+1;
      compensate = count;
    }
    else {
      pnt->begin -= compensate;
      pnt->end -= compensate;
      if ( !pnt->word.empty() ){
	pnt->word_index = ++new_index;
      }
    }
    pnt->link = resolve_mwus( pnt->link, new_index, compensate );
    pnt = pnt->next;
  }
  return result;
}

 vector<pair<string,int>> extract_dp( xmlDoc *alp_doc ){
  string txtfile = "/tmp/debug.xml";
  xmlSaveFormatFileEnc( txtfile.c_str(), alp_doc, "UTF8", 1 );
  xmlNode *top_node = TiCC::xPath( alp_doc, "//node[@rel='top']" );
  int index = 0;
  dp_tree *dp = parse_nodes( top_node, index );
  //  cerr << endl << "done parsing, dp nodes:" << endl;
  //  print_nodes( 0, dp );
  index = 0;
  int compensate = 0;
  dp = resolve_mwus( dp, index, compensate );
  cerr << endl << "done resolving, dp nodes:" << endl;
  print_nodes( 0, dp );
  vector<pair<string,int>> result;
  if ( dp->rel == "top" ){
    extract_dp( dp->link, 0, "BUG", result );
  }
  else {
    cerr << "PANIEK!, geen top node" << endl;
  }
  return result;
}

#endif

void Parser::Parse( frog_data& fd, TimerBlock& timers ){
  timers.parseTimer.start();
  if ( !isInit ){
    LOG << "Parser is not initialized! EXIT!" << endl;
    throw runtime_error( "Parser is not initialized!" );
  }
  if ( fd.empty() ){
    LOG << "unable to parse an analysis without words" << endl;
    return;
  }
#ifdef TEST_ALPINO_SERVER
  xmlDoc *parsed = alpino_server_parse( fd );
  cerr << "got XML" << endl;
  vector<pair<string,int>> solution = extract_dp( parsed );
  int count = 0;
  for( const auto& sol: solution ){
    cerr << ++count << "\t" << sol.second << "\t" << sol.first << endl;
  }
  cerr << endl;
#endif
  timers.prepareTimer.start();
  parseData pd = prepareParse( fd );
  timers.prepareTimer.stop();
  vector<timbl_result> p_results;
  vector<timbl_result> d_results;
  vector<timbl_result> r_results;
#pragma omp parallel sections
  {
#pragma omp section
      {
	timers.pairsTimer.start();
	vector<string> instances = createPairInstances( pd );
	if ( _host.empty() ){
	  timbl( pairs, instances, p_results );
	}
	else {
	  timbl_server( _pairs_base, instances, p_results );
	}
	timers.pairsTimer.stop();
      }
#pragma omp section
      {
	timers.dirTimer.start();
	vector<string> instances = createDirInstances( pd );
	if ( _host.empty() ){
	  timbl( dir, instances, d_results );
	}
	else {
	  timbl_server( _dirs_base, instances, d_results );
	}
	timers.dirTimer.stop();
      }
#pragma omp section
      {
	timers.relsTimer.start();
	vector<string> instances = createRelInstances( pd );
	if ( _host.empty() ){
	  timbl( rels, instances, r_results );
	}
	else {
	  timbl_server( _rels_base, instances, r_results );
	}
	timers.relsTimer.stop();
      }
  }

  timers.csiTimer.start();
  vector<parsrel> res = parse( p_results,
			       r_results,
			       d_results,
			       pd.words.size(),
			       maxDepSpan,
			       dbgLog );
  timers.csiTimer.stop();
  appendParseResult( fd, res );
  timers.parseTimer.stop();
}

void Parser::add_result( const frog_data& fd,
			 const vector<folia::Word*>& wv ) const {
  //  DBG << "Parser::add_result:" << endl << fd << endl;
  folia::Sentence *s = wv[0]->sentence();
  folia::KWargs args;
  if ( !s->id().empty() ){
    args["generate_id"] = s->id();
  }
  args["set"] = getTagset();
  // if ( textclass != "current" ){
  //   args["textclass"] = textclass;
  // }
  folia::DependenciesLayer *el = new folia::DependenciesLayer( args, s->doc() );
  s->append( el );
  for ( size_t pos=0; pos < fd.mw_units.size(); ++pos ){
    string cls = fd.mw_units[pos].parse_role;
    if ( cls != "ROOT" ){
      if ( !el->id().empty() ){
	args["generate_id"] = el->id();
      }
      args["class"] = cls;
      if ( textclass != "current" ){
	args["textclass"] = textclass;
      }
      args["set"] = getTagset();
      folia::Dependency *e = new folia::Dependency( args, s->doc() );
      el->append( e );
      args.clear();
      // if ( textclass != "current" ){
      // 	args["textclass"] = textclass;
      // }
      folia::Headspan *dh = new folia::Headspan( args );
      size_t head_index = fd.mw_units[pos].parse_index-1;
      for ( auto const& i : fd.mw_units[head_index].parts ){
	dh->append( wv[i] );
      }
      e->append( dh );
      folia::DependencyDependent *dd = new folia::DependencyDependent( args );
      for ( auto const& i : fd.mw_units[pos].parts ){
	dd->append( wv[i] );
      }
      e->append( dd );
    }
  }
}
