/*
  $Id$
  $URL$

  Copyright (c) 2006 - 2011
  Tilburg University

  This file is part of frog.

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

#ifndef __MBLEM_MOD__
#define __MBLEM_MOD__

class mblemData {
 public:
  mblemData( const std::string& l, const std::string& t ){
    lemma = l;
    tag   = t;
  };
  std::string getLemma() const { return lemma; };
  std::string getTag() const { return tag; };
 private:
  std::string lemma;
  std::string tag;
};

class Configuration;

class Mblem {
 public:
  Mblem();
  ~Mblem();
  bool init( const Configuration& );
  std::string Classify( folia::AbstractElement *, 
			const std::string& );
 private:
  void read_transtable( const std::string& );
  void create_MBlem_defaults();
  bool readsettings( const std::string& dir, const std::string& fname );
  std::string make_instance( const UnicodeString& in );
  std::string postprocess( const std::string& );
  Timbl::TimblAPI *myLex;
  std::string punctuation;
  size_t history;
  int debug;
  std::map <std::string,std::string> classMap;
  std::vector<mblemData> mblemResult;
};

#endif
