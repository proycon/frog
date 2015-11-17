/*
  $Id$
  $URL$

  Copyright (c) 2006 - 2015
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

#include <string>
#include <map>
#include "frog/clex.h"

using namespace std;

namespace CLEX {
  map<CLEX::Type,string> tagNames = {
    {CLEX::N, "noun" },
    {CLEX::A, "adjective"},
    {CLEX::Q, "quantifier/numeral"},
    {CLEX::V, "verb"},
    {CLEX::D, "article"},
    {CLEX::O, "pronoun"},
    {CLEX::B, "adverb"},
    {CLEX::P, "preposition"},
    {CLEX::C, "conjunction"},
    {CLEX::I, "interjection"},
    {CLEX::X, "unanalysed"},
    {CLEX::Z, "expression-part"},
    {CLEX::PN, "proper-noun"},
    {CLEX::AFFIX, "affix"},
    {CLEX::XAFFIX,"x-affix"},
    {CLEX::NEUTRAL, "neutral"},
    {CLEX::UNASS, "unassigned"}
  };

  map<char,string> iNames = {
    // the inflection names
    {'X', ""},
    {'s', "separated"},
    {'e', "singular"},
    {'m', "plural"},
    {'d', "diminutive"},
    {'G', "genitive"},
    {'D', "dative"},
    {'P', "positive"},
    {'C', "comparative"},
    {'S', "superlative"},
    {'E', "suffix-e"},
    {'i', "infinitive"},
    {'p', "participle"},
    {'t', "present_tense"},
    {'v', "past_tense"},
    {'1', "1st_person_verb"},
    {'2', "2nd_person_verb"},
    {'3', "3rd_person_verb"},
    {'I', "inversed"},
    {'g', "imperative"},
    {'a', "subjunctive"},
  };

  Type toCLEX( const string& s ){
    if ( s == "N" )
      return N;
    else if ( s == "A" )
      return A;
    else if ( s == "Q" )
      return Q;
    else if ( s == "V" )
      return V;
    else if ( s == "D" )
      return D;
    else if ( s == "O" )
      return O;
    else if ( s == "B" )
      return B;
    else if ( s == "P" )
      return P;
    else if ( s == "C" )
      return C;
    else if ( s == "I" )
      return I;
    else if ( s == "X" )
      return X;
    else if ( s == "Z" )
      return Z;
    else if ( s == "PN" )
      return PN;
    else if ( s == "*" )
      return AFFIX;
    else if ( s == "x" )
      return XAFFIX;
    else if ( s == "0" )
      return NEUTRAL;
    else
      return UNASS;
  }

  Type toCLEX( const char c ){
    string s;
    s += c;
    return toCLEX(s);
  }

  string toString( const Type& t ){
    switch ( t ){
    case N:
      return "N";
    case A:
      return "A";
    case Q:
      return "Q";
    case V:
      return "V";
    case D:
      return "D";
    case O:
      return "O";
    case B:
      return "B";
    case P:
      return "P";
    case C:
      return "C";
    case I:
      return "I";
    case X:
      return "X";
    case Z:
      return "Z";
    case PN:
      return "PN";
    case AFFIX:
      return "*";
    case XAFFIX:
      return "x";
    case NEUTRAL:
      return "0";
    default:
      return "/";
    }
  }

  bool isBasicClass( const Type& t ){
    switch ( t ){
    case N:
    case A:
    case Q:
    case V:
    case D:
    case O:
    case B:
    case P:
    case C:
    case I:
    case X:
    case Z:
      return true;
    default:
      return false;
    }
  }

  Type select_tag( const char ch ){
    Type result = CLEX::UNASS;
    switch( ch ){
    case 'm':
    case 'e':
    case 'd':
    case 'G':
    case 'D':
      result = N;
      break;
    case 'P':
    case 'C':
    case 'S':
    case 'E':
      result = A;
      break;
    case 'i':
    case 'p':
    case 't':
    case 'v':
    case 'g':
    case 'a':
      result = V;
      break;
    default:
      break;
    }
    return result;
  }

}

ostream& operator<<( ostream& os, const CLEX::Type& t ){
  os << toString( t );
  return os;
}
