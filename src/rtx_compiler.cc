#include <rtx_compiler.h>
#include <apertium/string_utils.h>
#include <apertium/utf_converter.h>

using namespace std;

wstring const
RTXCompiler::ANY_TAG = L"<ANY_TAG>";

wstring const
RTXCompiler::ANY_CHAR = L"<ANY_CHAR>";

RTXCompiler::RTXCompiler()
{
  longestPattern = 0;
  currentRule = NULL;
  currentChunk = NULL;
  currentChoice = NULL;
  errorsAreSyntax = true;
  inOutputRule = false;
  parserIsInChunk = false;
  inMacro = false;
  PB.starCanBeEmpty = true;
}

wstring const
RTXCompiler::SPECIAL_CHARS = L"!@$%()={}[]|/:;<>,.~→";

void
RTXCompiler::die(wstring message)
{
  if(errorsAreSyntax)
  {
    wcerr << L"Syntax error on line " << currentLine << L" of ";
  }
  else
  {
    wcerr << L"Error in rule beginning on line " << currentRule->line << L" of ";
  }
  wcerr << UtfConverter::fromUtf8(sourceFile) << L": " << message << endl;
  if(errorsAreSyntax && !source.eof())
  {
    wstring arr = wstring(recentlyRead.size()-2, L' ');
    while(!source.eof() && source.peek() != L'\n')
    {
      recentlyRead += source.get();
    }
    wcerr << recentlyRead << endl;
    wcerr << arr << L"^^^" << endl;
  }
  exit(EXIT_FAILURE);
}

void
RTXCompiler::eatSpaces()
{
  wchar_t c;
  bool inComment = false;
  while(!source.eof())
  {
    c = source.peek();
    if(c == L'\n')
    {
      source.get();
      inComment = false;
      currentLine++;
      recentlyRead.clear();
    }
    else if(inComment)
    {
      recentlyRead += source.get();
    }
    else if(isspace(c))
    {
      recentlyRead += source.get();
    }
    else if(c == L'!')
    {
      recentlyRead += source.get();
      inComment = true;
    }
    else
    {
      break;
    }
  }
}

wstring
RTXCompiler::nextTokenNoSpace()
{
  if(source.eof())
  {
    die(L"Unexpected end of file");
  }
  wchar_t c = source.get();
  wchar_t next = source.peek();
  wstring ret;
  if(c == L'→')
  {
    recentlyRead += c;
    ret = L"->";
  }
  else if(SPECIAL_CHARS.find(c) != string::npos)
  {
    ret = wstring(1, c);
    recentlyRead += c;
  }
  else if(c == L'-' && next == L'>')
  {
    next = source.get();
    ret = wstring(1, c) + wstring(1, next);
    recentlyRead += ret;
  }
  else if(isspace(c))
  {
    die(L"unexpected space");
  }
  else if(c == L'!')
  {
    die(L"unexpected comment");
  }
  else
  {
    ret = wstring(1, c);
    while(!source.eof())
    {
      c = source.peek();
      if(c == L'\\')
      {
        ret += source.get();
        ret += source.get();
      }
      else if(SPECIAL_CHARS.find(c) == string::npos && !isspace(c))
      {
        ret += wstring(1, source.get());
      }
      else
      {
        break;
      }
    }
    recentlyRead += ret;
  }
  return ret;
}

bool
RTXCompiler::isNextToken(wchar_t c)
{
  if(source.peek() == c)
  {
    recentlyRead += source.get();
    return true;
  }
  return false;
}

wstring
RTXCompiler::nextToken(wstring check1 = L"", wstring check2 = L"")
{
  eatSpaces();
  wstring tok = nextTokenNoSpace();
  if(tok == check1 || tok == check2 || (check1 == L"" && check2 == L""))
  {
  }
  else if(check1 != L"" && check2 != L"")
  {
    die(L"expected '" + check1 + L"' or '" + check2 + L"', found '" + tok + L"'");
  }
  else if(check1 != L"")
  {
    die(L"expected '" + check1 + L"', found '" + tok + L"'");
  }
  else
  {
    die(L"expected '" + check2 + L"', found '" + tok + L"'");
  }
  return tok;
}

wstring
RTXCompiler::parseIdent(bool prespace = false)
{
  if(prespace)
  {
    eatSpaces();
  }
  wstring ret = nextTokenNoSpace();
  if(ret == L"->" || (ret.size() == 1 && SPECIAL_CHARS.find(ret[0]) != string::npos))
  {
    die(L"expected identifier, found '" + ret + L"'");
  }
  return ret;
}

unsigned int
RTXCompiler::parseInt()
{
  wstring ret;
  while(isdigit(source.peek()))
  {
    ret += source.get();
  }
  recentlyRead += ret;
  return stoul(ret);
}

float
RTXCompiler::parseWeight()
{
  wstring ret;
  while(isdigit(source.peek()) || source.peek() == L'.')
  {
    ret += source.get();
  }
  recentlyRead += ret;
  try
  {
    wstring::size_type loc;
    float r = stof(ret, &loc);
    if(loc == ret.size())
    {
      return r;
    }
    else
    {
      die(L"unable to parse weight: " + ret);
    }
  }
  catch(const invalid_argument& ia)
  {
    die(L"unable to parse weight: " + ret);
  }
}

void
RTXCompiler::parseRule()
{
  wstring firstLabel = parseIdent();
  wstring next = nextToken();
  if(next == L":")
  {
    parseOutputRule(firstLabel);
  }
  else if(next == L">")
  {
    parseRetagRule(firstLabel);
  }
  else if(next == L"=")
  {
    parseAttrRule(firstLabel);
  }
  else
  {
    parseReduceRule(firstLabel, next);
  }
}

void
RTXCompiler::parseOutputRule(wstring pattern)
{
  nodeIsSurface[pattern] = !isNextToken(L':');
  eatSpaces();
  vector<wstring> output;
  if(source.peek() == L'(')
  {
    inMacro = true;
    parserIsInChunk = true;
    currentRule = new Rule;
    currentRule->pattern.push_back(vector<wstring>(2, L"blah"));
    parseOutputCond();
    macros[pattern] = currentChoice;
    currentChoice = NULL;
    output.push_back(L"macro");
    currentRule = NULL;
    parserIsInChunk = false;
    inMacro = false;
    nextToken(L";");
  }
  else
  {
    wstring cur;
    while(!source.eof())
    {
      cur = nextToken();
      if(cur == L"<")
      {
        cur = cur + parseIdent();
        cur += nextToken(L">");
      }
      output.push_back(cur);
      if(nextToken(L".", L";") == L";")
      {
        break;
      }
    }
    if(output.size() == 0)
    {
      die(L"empty tag order rule");
    }
  }
  outputRules[pattern] = output;
}

void
RTXCompiler::parseRetagRule(wstring srcTag)
{
  wstring destTag = parseIdent(true);
  nextToken(L":");
  vector<pair<wstring, wstring>> rule;
  rule.push_back(pair<wstring, wstring>(srcTag, destTag));
  while(!source.eof())
  {
    eatSpaces();
    bool list = isNextToken(L'[');
    wstring cs = parseIdent(true);
    if(list)
    {
      nextToken(L"]");
      cs = L"[]" + cs;
    }
    wstring cd = parseIdent(true);
    rule.push_back(pair<wstring, wstring>(cs, cd));
    if(nextToken(L";", L",") == L";")
    {
      break;
    }
  }
  retagRules.push_back(rule);
  if(altAttrs.find(destTag) == altAttrs.end())
  {
    altAttrs[destTag].push_back(destTag);
  }
  altAttrs[destTag].push_back(srcTag);
}

void
RTXCompiler::parseAttrRule(wstring categoryName)
{
  if(collections.find(categoryName) != collections.end())
  {
    die(L"redefinition of category " + categoryName);
  }
  eatSpaces();
  if(isNextToken(L'('))
  {
    wstring undef = parseIdent(true);
    wstring def = parseIdent(true);
    attrDefaults[categoryName] = make_pair(undef, def);
    nextToken(L")");
  }
  vector<wstring> members;
  vector<wstring> noOver;
  while(true)
  {
    eatSpaces();
    if(isNextToken(L';'))
    {
      break;
    }
    if(isNextToken(L'['))
    {
      wstring other = parseIdent(true);
      if(collections.find(other) == collections.end())
      {
        die(L"Use of category '" + other + L"' in set arithmetic before definition.");
      }
      vector<wstring> otherstuff = collections[other];
      for(unsigned int i = 0; i < otherstuff.size(); i++)
      {
        members.push_back(otherstuff[i]);
      }
      otherstuff = noOverwrite[other];
      for(unsigned int i = 0; i < otherstuff.size(); i++)
      {
        noOver.push_back(otherstuff[i]);
      }
      nextToken(L"]");
    }
    else if(isNextToken(L'@'))
    {
      wstring next = parseIdent();
      members.push_back(next);
      noOver.push_back(next);
    }
    else
    {
      members.push_back(parseIdent());
    }
  }
  if(members.size() == 0)
  {
    die(L"empty attribute list");
  }
  collections.insert(pair<wstring, vector<wstring>>(categoryName, members));
  noOverwrite.insert(pair<wstring, vector<wstring>>(categoryName, noOver));
  if(noOver.size() > 0)
  {
    for(unsigned int i = 0; i < noOver.size(); i++)
    {
      noOver[i] = L"<" + noOver[i] + L">";
    }
  }
  collections.insert(make_pair(categoryName + L" over", noOver));
}

RTXCompiler::Clip*
RTXCompiler::parseClip(int src = -2)
{
  Clip* ret = new Clip;
  eatSpaces();
  if(src != -2)
  {
    ret->src = src;
  }
  else if(isdigit(source.peek()))
  {
    ret->src = parseInt();
    nextToken(L".");
  }
  else if(isNextToken(L'$'))
  {
    ret->src = -1;
  }
  else
  {
    ret->src = 0;
  }
  if(inMacro && !(ret->src == 0 || ret->src == 1))
  {
    die(L"Macros can only access their single argument.");
  }
  else if(src == -2 && ret->src > (int)currentRule->pattern.size())
  {
    die(L"Clip source is out of bounds (position " + to_wstring(ret->src) + L" requested, but rule has only " + to_wstring(currentRule->pattern.size()) + L" elements in its pattern).");
  }
  ret->part = parseIdent();
  if(isNextToken(L'/'))
  {
    if(ret->src == 0)
    {
      die(L"literal value cannot have a side");
    }
    else if(ret->src == -1)
    {
      die(L"variable cannot have a side");
    }
    ret->side = parseIdent();
  }
  if(isNextToken(L'>'))
  {
    if(ret->src == 0)
    {
      die(L"literal value cannot be rewritten");
    }
    else if(ret->src == -1)
    {
      die(L"variable cannot be rewritten");
    }
    ret->rewrite = parseIdent();
  }
  return ret;
}

RTXCompiler::Cond*
RTXCompiler::parseCond()
{
  Cond* ret = new Cond;
  ret->op = 0;
  nextToken(L"(");
  eatSpaces();
  if(isNextToken(L'~'))
  {
    Cond* left = new Cond;
    left->op = NOT;
    left->right = parseCond();
    ret->left = left;
  }
  else if(!source.eof() && source.peek() == L'(')
  {
    ret->left = parseCond();
  }
  else
  {
    Cond* left = new Cond;
    left->op = 0;
    left->val = parseClip();
    ret->left = left;
  }
  while(true)
  {
    bool isNotted = false;
    wstring op = nextToken();
    if(op == L")")
    {
      return ret->left;
    }
    else
    {
      if(op == L"not")
      {
        isNotted = true;
        op = nextToken();
      }
      bool found = false;
      wstring key = StringUtils::tolower(op);
      key = StringUtils::substitute(key, L"-", L"");
      key = StringUtils::substitute(key, L"_", L"");
      for(unsigned int i = 0; i < OPERATORS.size(); i++)
      {
        if(key == OPERATORS[i].first)
        {
          ret->op = OPERATORS[i].second;
          found = true;
          break;
        }
      }
      if(!found)
      {
        die(L"unknown operator '" + op + L"'");
      }
    }
    eatSpaces();
    if(!source.eof() && source.peek() == L'(')
    {
      ret->right = parseCond();
    }
    else if(isNextToken(L'~'))
    {
      eatSpaces();
      ret->right = new Cond;
      ret->right->op = NOT;
      ret->right->right = parseCond();
    }
    else
    {
      ret->right = new Cond;
      ret->right->op = 0;
      ret->right->val = parseClip();
    }
    Cond* temp = ret;
    if(isNotted)
    {
      ret = new Cond;
      ret->op = NOT;
      ret->right = temp;
      temp = ret;
    }
    ret = new Cond;
    ret->left = temp;
  }
}

void
RTXCompiler::parsePatternElement(Rule* rule)
{
  vector<wstring> pat;
  if(isNextToken(L'%'))
  {
    rule->grab_all = rule->pattern.size()+1;
  }
  wstring t1 = nextToken();
  if(t1 == L"$")
  {
    t1 += parseIdent();
  }
  if(isNextToken(L'@'))
  {
    pat.push_back(t1);
    pat.push_back(parseIdent());
  }
  else if(t1[0] == L'$')
  {
    die(L"first tag in pattern element must be literal");
  }
  else
  {
    pat.push_back(L"");
    pat.push_back(t1);
  }
  while(!source.eof())
  {
    if(!isNextToken(L'.'))
    {
      break;
    }
    wstring cur = nextToken();
    if(cur == L"$")
    {
      Clip* cl = parseClip(rule->pattern.size()+1);
      if(rule->vars.find(cl->part) != rule->vars.end())
      {
        die(L"rule has multiple sources for attribute " + cl->part);
      }
      rule->vars[cl->part] = cl;
    }
    else
    {
      pat.push_back(cur);
    }
  }
  rule->pattern.push_back(pat);
  eatSpaces();
}

void
RTXCompiler::parseOutputElement()
{
  OutputChunk* ret = new OutputChunk;
  ret->conjoined = isNextToken(L'+');
  ret->nextConjoined = false;
  if(ret->conjoined)
  {
    if(currentChunk == NULL)
    {
      die(L"Cannot conjoin from within if statement.");
    }
    if(currentChunk->children.size() == 0)
    {
      die(L"Cannot conjoin first element.");
    }
    if(currentChunk->children.back()->conds.size() > 0)
    {
      die(L"Cannot conjoin to something in an if statement.");
    }
    if(currentChunk->children.back()->chunks.size() == 0)
    {
      die(L"Cannot conjoin inside and outside of if statement and cannot conjoin first element.");
    }
    if(currentChunk->children.back()->chunks[0]->mode == L"_")
    {
      die(L"Cannot conjoin to a blank.");
    }
    eatSpaces();
    currentChunk->children.back()->chunks[0]->nextConjoined = true;
  }
  ret->getall = isNextToken(L'%');
  if(source.peek() == L'_')
  {
    if(ret->getall)
    {
      die(L"% cannot be used on blanks");
    }
    ret->mode = L"_";
    recentlyRead += source.get();
    if(isdigit(source.peek()))
    {
      ret->pos = parseInt();
      if(currentRule->pattern.size() == 1)
      {
        die(L"Cannot output indexed blank because pattern is one element long and thus does not include blanks.");
      }
      if(ret->pos < 1 || ret->pos >= currentRule->pattern.size())
      {
        die(L"Position index of blank out of bounds, expected an integer from 1 to " + to_wstring(currentRule->pattern.size()-1) + L".");
      }
    }
    else
    {
      ret->pos = 0;
    }
  }
  else if(isdigit(source.peek()))
  {
    ret->mode = L"#";
    ret->pos = parseInt();
    if(ret->pos == 0)
    {
      die(L"There is no position 0.");
    }
    else if(ret->pos > currentRule->pattern.size())
    {
      die(L"There are only " + to_wstring(currentRule->pattern.size()) + L" elements in the pattern.");
    }
    if(source.peek() == L'[')
    {
      nextToken(L"[");
      ret->pattern = parseIdent();
      nextToken(L"]");
    }
    else
    {
      ret->pattern = currentRule->pattern[ret->pos-1][1];
    }
  }
  else if(isNextToken(L'*'))
  {
    if(source.peek() != L'[')
    {
      die(L"No macro name specified.");
    }
    nextToken(L"[");
    ret->pattern = parseIdent(true);
    nextToken(L"]");
    ret->pos = 0;
    ret->mode = L"#";
  }
  else
  {
    if(ret->getall)
    {
      die(L"% not currently supported on output literals");
    }
    ret->lemma = parseIdent();
    ret->mode = nextToken(L"@");
    while(true)
    {
      wstring cur = nextToken();
      wstring var = to_wstring(ret->tags.size());
      ret->tags.push_back(var);
      Clip* cl = new Clip;
      if(cur == L"$")
      {
        cl->src = -1;
        cl->part = parseIdent();
      }
      else if(cur == L"[")
      {
        cl = parseClip();
        nextToken(L"]");
      }
      else
      {
        cl->src = 0;
        cl->part = cur;
      }
      ret->vars[var] = cl;
      if(!isNextToken(L'.'))
      {
        break;
      }
    }
  }
  if(isNextToken(L'('))
  {
    while(!source.eof() && source.peek() != L')')
    {
      eatSpaces();
      wstring var = parseIdent();
      nextToken(L"=");
      eatSpaces();
      Clip* cl = parseClip();
      if(cl->part == L"_")
      {
        cl->part = L"";
      }
      ret->vars[var] = cl;
      if(nextToken(L",", L")") == L")")
      {
        break;
      }
    }
  }
  if(currentChoice != NULL)
  {
    currentChoice->chunks.push_back(ret);
    currentChoice->nest.push_back(NULL);
  }
  else
  {
    OutputChoice* temp = new OutputChoice;
    temp->chunks.push_back(ret);
    temp->nest.push_back(NULL);
    if(currentChunk == NULL)
    {
      currentRule->output.push_back(temp);
    }
    else
    {
      currentChunk->children.push_back(temp);
    }
  }
  eatSpaces();
}

void
RTXCompiler::parseOutputCond()
{
  nextToken(L"(");
  OutputChoice* choicewas = currentChoice;
  OutputChunk* chunkwas = currentChunk;
  OutputChoice* ret = new OutputChoice;
  currentChoice = ret;
  currentChunk = NULL;
  while(true)
  {
    wstring mode = StringUtils::tolower(nextToken());
    mode = StringUtils::substitute(mode, L"-", L"");
    mode = StringUtils::substitute(mode, L"_", L"");
    if(ret->conds.size() == 0 && mode != L"if" && mode != L"always")
    {
      die(L"If statement must begin with 'if'.");
    }
    if(ret->conds.size() > 0 && mode == L"always")
    {
      die(L"Always clause must be only clause.");
    }
    if(mode == L"if" || mode == L"elif" || mode == L"elseif")
    {
      ret->conds.push_back(parseCond());
    }
    else if(mode == L")")
    {
      break;
    }
    else if(mode != L"else" && mode != L"otherwise" && mode != L"always")
    {
      die(L"Unknown statement: '" + mode + L"'.");
    }
    eatSpaces();
    if(source.peek() == L'(')
    {
      parseOutputCond();
      ret->chunks.push_back(NULL);
    }
    else if(source.peek() == L'{')
    {
      if(parserIsInChunk)
      {
        die(L"Nested chunks are currently not allowed.");
      }
      ret->nest.push_back(NULL);
      parseOutputChunk();
    }
    else if(source.peek() == L'[')
    {
      ret->nest.push_back(NULL);
      parseOutputChunk();
    }
    else
    {
      if(!parserIsInChunk)
      {
        die(L"Conditional non-chunk output current not possible.");
      }
      parseOutputElement();
      ret->nest.push_back(NULL);
    }
    if(mode == L"else" || mode == L"otherwise" || mode == L"always")
    {
      nextToken(L")");
      break;
    }
  }
  currentChunk = chunkwas;
  currentChoice = choicewas;
  if(ret->chunks.size() == 0)
  {
    die(L"If statement cannot be empty.");
  }
  if(ret->conds.size() == ret->nest.size())
  {
    //die(L"If statement has no else clause and thus could produce no output.");
    ret->nest.push_back(NULL);
    OutputChunk* temp = new OutputChunk;
    temp->mode = L"[]";
    temp->pos = 0;
    ret->chunks.push_back(temp);
  }
  eatSpaces();
  if(currentChoice != NULL)
  {
    currentChoice->nest.push_back(ret);
    currentChoice->chunks.push_back(NULL);
  }
  else if(currentChunk != NULL)
  {
    currentChunk->children.push_back(ret);
  }
  else if(inMacro)
  {
    currentChoice = ret;
  }
  else
  {
    currentRule->output.push_back(ret);
  }
}

void
RTXCompiler::parseOutputChunk()
{
  wchar_t end;
  OutputChunk* ch = new OutputChunk;
  if(nextToken(L"{", L"[") == L"{")
  {
    parserIsInChunk = true;
    ch->mode = L"{}";
    end = L'}';
  }
  else
  {
    if(!parserIsInChunk)
    {
      die(L"Output grouping with [] only valid inside chunks.");
    }
    ch->mode == L"[]";
    end = L']';
  }
  eatSpaces();
  OutputChunk* chunkwas = currentChunk;
  OutputChoice* choicewas = currentChoice;
  currentChunk = ch;
  currentChoice = NULL;
  ch->pos = 0;
  while(source.peek() != end)
  {
    if(source.peek() == L'(')
    {
      parseOutputCond();
    }
    else
    {
      parseOutputElement();
    }
  }
  nextToken(wstring(1, end));
  if(end == L'}') parserIsInChunk = false;
  eatSpaces();
  currentChunk = chunkwas;
  currentChoice = choicewas;
  OutputChoice* ret = new OutputChoice;
  ret->chunks.push_back(ch);
  ret->nest.push_back(NULL);
  if(currentChoice != NULL)
  {
    currentChoice->chunks.push_back(ch);
  }
  else if(currentChunk != NULL)
  {
    currentChunk->children.push_back(ret);
  }
  else
  {
    currentRule->output.push_back(ret);
  }
}

void
RTXCompiler::parseReduceRule(wstring output, wstring next)
{
  vector<wstring> outNodes;
  outNodes.push_back(output);
  if(next != L"->")
  {
    wstring cur = next;
    while(cur != L"->")
    {
      if(SPECIAL_CHARS.find(cur) != wstring::npos)
      {
        die(L"Chunk names must be identifiers. (I think I'm parsing a reduction rule.)");
      }
      outNodes.push_back(cur);
      cur = nextToken();
    }
  }
  Rule* rule;
  while(true)
  {
    rule = new Rule();
    currentRule = rule;
    rule->grab_all = -1;
    rule->result = outNodes;
    eatSpaces();
    rule->line = currentLine;
    if(isdigit(source.peek()))
    {
      rule->weight = parseWeight();
      nextToken(L":");
      eatSpaces();
    }
    else
    {
      rule->weight = 0;
    }
    while(!source.eof() && source.peek() != L'{' && source.peek() != L'[' && source.peek() != L'(' && source.peek() != L'?')
    {
      parsePatternElement(rule);
    }
    if(rule->pattern.size() == 0)
    {
      die(L"empty pattern");
    }
    eatSpaces();
    if(isNextToken(L'?'))
    {
      rule->cond = parseCond();
      eatSpaces();
    }
    if(isNextToken(L'['))
    {
      while(!source.eof())
      {
        nextToken(L"$");
        wstring var = parseIdent();
        if(rule->vars.find(var) != rule->vars.end())
        {
          die(L"rule has multiple sources for attribute " + var);
        }
        nextToken(L"=");
        rule->vars[var] = parseClip();
        if(nextToken(L",", L"]") == L"]")
        {
          break;
        }
      }
      eatSpaces();
    }
    if(rule->result.size() > 1)
    {
      nextToken(L"{");
    }
    int chunk_count = 0;
    while(chunk_count < rule->result.size())
    {
      eatSpaces();
      if(source.eof()) die(L"Unexpected end of file.");
      switch(source.peek())
      {
        case L'(':
          parseOutputCond();
          chunk_count++;
          break;
        case L'{':
          parseOutputChunk();
          chunk_count++;
          break;
        case L'_':
          parseOutputElement();
          break;
        case L'}':
          if(rule->result.size() == 1)
          {
            die(L"Unexpected } in output pattern.");
          }
          else if(chunk_count < rule->result.size())
          {
            die(L"Output pattern does not have enough chunks.");
          }
          break;
        default:
          parseOutputElement();
          chunk_count++;
          break;
      }
    }
    if(rule->result.size() > 1)
    {
      nextToken(L"}");
    }
    reductionRules.push_back(rule);
    if(nextToken(L"|", L";") == L";")
    {
      break;
    }
  }
}

void
RTXCompiler::makePattern(int ruleid)
{
  Rule* rule = reductionRules[ruleid];
  if(rule->pattern.size() > longestPattern)
  {
    longestPattern = rule->pattern.size();
  }
  vector<vector<PatternElement*>> pat;
  for(unsigned int i = 0; i < rule->pattern.size(); i++)
  {
    vector<wstring> tags;
    for(unsigned int j = 1; j < rule->pattern[i].size(); j++)
    {
      tags.push_back(rule->pattern[i][j]);
    }
    tags.push_back(L"*");
    wstring lem = rule->pattern[i][0];
    if(lem.size() == 0 || lem[0] != L'$')
    {
      PatternElement* p = new PatternElement;
      p->lemma = lem;
      p->tags = tags;
      pat.push_back(vector<PatternElement*>(1, p));
    }
    else
    {
      vector<wstring> lems = collections[lem.substr(1)];
      vector<PatternElement*> el;
      for(unsigned int j = 0; j < lems.size(); j++)
      {
        PatternElement* p = new PatternElement;
        p->lemma = lems[j];
        p->tags = tags;
        el.push_back(p);
      }
      pat.push_back(el);
    }
  }
  PB.addPattern(pat, ruleid+1, rule->weight);
}

wstring
RTXCompiler::compileString(wstring s)
{
  wstring ret;
  ret += STRING;
  ret += (wchar_t)s.size();
  ret += s;
  return ret;
}

wstring
RTXCompiler::compileTag(wstring s)
{
  if(s.size() == 0)
  {
    return compileString(s);
  }
  wstring tag;
  tag += L'<';
  tag += s;
  tag += L'>';
  return compileString(tag);
}

wstring
RTXCompiler::compileClip(Clip* c, wstring _dest = L"")
{
  int src = (c->src == -1) ? 0 : c->src;
  bool useReplace = inOutputRule;
  wstring dest;
  if(inOutputRule && _dest.size() > 0)
  {
    dest = _dest;
  }
  else if(c->rewrite.size() > 0)
  {
    dest = c->rewrite;
  }
  wstring cl = (c->part == L"lemcase") ? compileString(L"lem") : compileString(c->part);
  cl += INT;
  cl += src;
  wstring ret = cl;
  wstring undeftag;
  wstring deftag;
  wstring thedefault;
  wstring blank;
  blank += DUP;
  blank += compileString(L"");
  blank += EQUAL;
  if(useReplace && undeftag.size() > 0)
  {
    blank += OVER;
    blank += compileTag(undeftag);
    blank += EQUAL;
    blank += OR;
  }
  if(attrDefaults.find(c->part) != attrDefaults.end())
  {
    undeftag = attrDefaults[c->part].first;
    deftag = attrDefaults[c->part].second;
    thedefault += DROP;
    thedefault += compileTag(useReplace ? deftag : undeftag);
    if(useReplace)
    {
      blank += OVER;
      blank += compileTag(undeftag);
      blank += EQUAL;
      blank += OR;
    }
  }
  blank += JUMPONFALSE;
  if(c->src == 0)
  {
    return compileTag(c->part);
  }
  else if(c->side == L"sl")
  {
    ret += SOURCECLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  else if(c->side == L"ref")
  {
    ret += REFERENCECLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  else if(c->side == L"tl" || c->part == L"lemcase" ||
          (c->src != -1 && !nodeIsSurface[currentRule->pattern[c->src-1][1]]))
  {
    ret += TARGETCLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  else
  {
    ret += TARGETCLIP;
    ret += blank;
    ret += (wchar_t)(6 + 2*cl.size() + 2*blank.size() + thedefault.size());
    ret += DROP;
    ret += cl;
    ret += REFERENCECLIP;
    ret += blank;
    ret += (wchar_t)(3 + cl.size() + blank.size() + thedefault.size());
    ret += DROP;
    ret += cl;
    ret += SOURCECLIP;
    ret += blank;
    ret += (wchar_t)thedefault.size();
    ret += thedefault;
  }
  if(c->part == L"lemcase")
  {
    ret += GETCASE;
  }
  if(dest.size() > 0)
  {
    bool found = false;
    vector<pair<wstring, wstring>> rule;
    for(unsigned int i = 0; i < retagRules.size(); i++)
    {
      if(retagRules[i][0].first == c->part && retagRules[i][0].second == dest)
      {
        found = true;
        rule = retagRules[i];
        break;
      }
    }
    if(!found && dest != c->part)
    {
      die(L"There is no tag-rewrite rule from '" + c->part + L"' to '" + dest + L"'.");
    }
    wstring check;
    for(unsigned int i = 1; i < rule.size(); i++)
    {
      wstring cur;
      cur += DUP;
      if(rule[i].first.size() > 2 &&
         rule[i].first[0] == L'[' && rule[i].first[1] == L']')
      {
        cur += compileString(rule[i].first.substr(2));
        cur += IN;
      }
      else
      {
        cur += compileTag(rule[i].first);
        cur += EQUAL;
      }
      cur += JUMPONFALSE;
      cur += (wchar_t)(rule[i].second.size() + (i == 1 ? 5 : 7));
      cur += DROP;
      cur += compileTag(rule[i].second);
      if(i != 1)
      {
        cur += JUMP;
        cur += (wchar_t)check.size();
      }
      check = cur + check;
    }
    ret += check;
  }
  return ret;
}

wstring
RTXCompiler::compileClip(wstring part, int pos, wstring side = L"")
{
  Clip cl;
  cl.part = part;
  cl.src = pos;
  cl.side = side;
  return compileClip(&cl);
}

RTXCompiler::Clip*
RTXCompiler::processMacroClip(Clip* mac, OutputChunk* arg)
{
  Clip* ret = new Clip;
  ret->part = mac->part;
  ret->side = mac->side;
  ret->rewrite = mac->rewrite;
  if(arg->pos == 0 && mac->src == 1)
  {
    if(arg->vars.find(mac->part) != arg->vars.end())
    {
      Clip* other = arg->vars[mac->part];
      ret->part = other->part;
      ret->side = other->side;
      ret->rewrite = other->rewrite; // TODO: what if they both have rewrite?
      ret->src = other->src;
    }
    else
    {
      die(L"Macro not given value for attribute '" + mac->part + L"'.");
    }
  }
  else
  {
    ret->src = (mac->src == 1) ? arg->pos : mac->src;
  }
  return ret;
}

RTXCompiler::Cond*
RTXCompiler::processMacroCond(Cond* mac, OutputChunk* arg)
{
  Cond* ret = new Cond;
  ret->op = mac->op;
  if(mac->op == 0)
  {
    ret->val = processMacroClip(mac->val, arg);
  }
  else
  {
    ret->left = processMacroCond(mac->left, arg);
    ret->right = processMacroCond(mac->right, arg);
  }
  return ret;
}

RTXCompiler::OutputChunk*
RTXCompiler::processMacroChunk(OutputChunk* mac, OutputChunk* arg)
{
  if(mac == NULL) return NULL;
  OutputChunk* ret = new OutputChunk;
  ret->mode = mac->mode;
  ret->lemma = mac->lemma;
  ret->tags = mac->tags;
  ret->getall = mac->getall;
  ret->pattern = mac->pattern;
  ret->conjoined = mac->conjoined;
  ret->nextConjoined = mac->nextConjoined;
  for(unsigned int i = 0; i < mac->children.size(); i++)
  {
    ret->children.push_back(processMacroChoice(mac->children[i], arg));
  }
  for(map<wstring, Clip*>::iterator it = mac->vars.begin(),
          limit = mac->vars.end(); it != limit; it++)
  {
    ret->vars[it->first] = processMacroClip(it->second, arg);
  }
  if(mac->pos == 1)
  {
    ret->pos = arg->pos;
    for(map<wstring, Clip*>::iterator it = arg->vars.begin(),
            limit = arg->vars.end(); it != limit; it++)
    {
      if(ret->vars.find(it->first) == ret->vars.end() || arg->pos == 0)
      {
        ret->vars[it->first] = it->second;
      }
    }
  }
  else
  {
    ret->pos = mac->pos;
  }
  return ret;
}

RTXCompiler::OutputChoice*
RTXCompiler::processMacroChoice(OutputChoice* mac, OutputChunk* arg)
{
  if(mac == NULL) return NULL;
  OutputChoice* ret = new OutputChoice;
  for(unsigned int i = 0; i < mac->conds.size(); i++)
  {
    ret->conds.push_back(processMacroCond(mac->conds[i], arg));
  }
  for(unsigned int i = 0; i < mac->nest.size(); i++)
  {
    ret->nest.push_back(processMacroChoice(mac->nest[i], arg));
  }
  for(unsigned int i = 0; i < mac->chunks.size(); i++)
  {
    ret->chunks.push_back(processMacroChunk(mac->chunks[i], arg));
  }
  return ret;
}

wstring
RTXCompiler::processOutputChunk(OutputChunk* r)
{
  wstring ret;
  if(r->mode == L"_")
  {
    ret += INT;
    ret += (wchar_t)r->pos;
    ret += BLANK;
    if(inOutputRule)
    {
      ret += OUTPUT;
    }
  }
  else if(r->mode == L"{}" || r->mode == L"[]" || r->mode == L"")
  {
    for(unsigned int i = 0; i < r->children.size(); i++)
    {
      ret += processOutputChoice(r->children[i]);
    }
  }
  else if(r->mode == L"#")
  {
    wstring pos;
    if(r->pos != 0)
    {
      if(currentRule->pattern[r->pos-1].size() < 2)
      {
        die(L"could not find tag order for element " + to_wstring(r->pos));
      }
      wstring pos = currentRule->pattern[r->pos-1][1];
    }
    wstring patname = (r->pattern != L"") ? r->pattern : pos;
    pos = (pos != L"") ? pos : patname;
    if(outputRules.find(patname) == outputRules.end())
    {
      die(L"could not find output pattern '" + patname + L"'");
    }
    vector<wstring> pattern = outputRules[patname];

    if(r->getall)
    {
      for(unsigned int i = 0; i < parentTags.size(); i++)
      {
        if(r->vars.find(parentTags[i]) == r->vars.end())
        {
          Clip* cl = new Clip;
          cl->src = -1;
          cl->part = parentTags[i];
          r->vars[parentTags[i]] = cl;
        }
      }
    }

    if(pattern.size() == 1 && pattern[0] == L"macro")
    {
      if(r->nextConjoined)
      {
        die(L"Cannot currently conjoin to a macro.");
      }
      if(r->conjoined)
      {
        die(L"Cannot currently conjoin a macro.");
      }
      return processOutputChoice(processMacroChoice(macros[patname], r));
    }
    else if(r->conjoined)
    {
      ret += compileString(L"+");
      ret += APPENDSURFACE;
    }
    else
    {
      ret += CHUNK;
    }
    for(unsigned int i = 0; i < pattern.size(); i++)
    {
      if(pattern[i] == L"_")
      {
        if(r->vars.find(L"lem") != r->vars.end())
        {
          ret += compileClip(r->vars[L"lem"], L"lem");
        }
        else if(r->vars.find(L"lemh") != r->vars.end())
        {
          ret += compileClip(r->vars[L"lemh"], L"lemh");
        }
        else if(r->pos == 0)
        {
          ret += compileString(L"unknown");
        }
        else
        {
          Clip* c = new Clip;
          c->part = L"lemh";
          c->src = r->pos;
          c->side = L"tl";
          c->rewrite = L"lemh";
          ret += compileClip(c);
        }
        if(r->vars.find(L"lemcase") != r->vars.end())
        {
          ret += compileClip(r->vars[L"lemcase"], L"lemcase");
          ret += SETCASE;
        }
        ret += APPENDSURFACE;
        if(r->pos != 0)
        {
          //ret += compileTag(currentRule->pattern[r->pos-1][1]);
          ret += compileClip(L"pos_tag", r->pos, L"tl");
        }
        else
        {
          ret += compileTag(pos);
        }
        ret += APPENDSURFACE;
      }
      else if(pattern[i][0] == L'<')
      {
        ret += compileString(pattern[i]);
        ret += APPENDSURFACE;
      }
      else
      {
        wstring ret_temp;
        vector<wstring> ops = altAttrs[pattern[i]];
        if(ops.size() == 0)
        {
          ops.push_back(pattern[i]);
        }
        wstring var;
        for(unsigned int v = 0; v < ops.size(); v++)
        {
          if(r->vars.find(ops[v]) != r->vars.end())
          {
            var = ops[v];
            break;
          }
        }
        if(var == L"" && r->pos != 0)
        {
          Clip* cl = new Clip;
          cl->src = r->pos;
          cl->part = pattern[i];
          ret_temp += compileClip(cl, pattern[i]);
        }
        else if(var == L"")
        {
          bool found = false;
          for(unsigned int t = 0; t < parentTags.size(); t++)
          {
            if(parentTags[t] == pattern[i])
            {
              ret_temp += compileTag(to_wstring(t+1));
              found = true;
              break;
            }
          }
          if(!found)
          {
            if(r->pos == 0 && currentRule->grab_all != -1)
            {
              ret_temp += compileClip(pattern[i], currentRule->grab_all);
            }
            else if(attrDefaults.find(pattern[i]) != attrDefaults.end())
            {
              ret_temp += compileTag(attrDefaults[pattern[i]].first);
            }
            else if(r->pos == 0)
            {
              die(L"Cannot find source for tag '" + pattern[i] + L"'.");
            }
            else
            {
              ret_temp += compileClip(pattern[i], r->pos);
            }
          }
        }
        else
        {
          ret_temp += compileClip(r->vars[var], pattern[i]);
        }
        if(inOutputRule && noOverwrite[pattern[i]].size() > 0)
        {
          ret += compileClip(pattern[i], r->pos, L"tl");
          ret += DUP;
          ret += compileString(pattern[i] + L" over");
          ret += IN;
          ret += JUMPONTRUE;
          ret += (wchar_t)(1+ret_temp.size());
          ret += DROP;
        }
        ret += ret_temp;
        ret += APPENDSURFACE;
      }
    }
    if(r->vars.find(L"lemq") != r->vars.end())
    {
      ret += compileClip(r->vars[L"lemq"]);
      ret += APPENDSURFACE;
    }
    else if(r->pos != 0)
    {
      ret += compileClip(L"lemq", r->pos, L"tl");
      ret += APPENDSURFACE;
    }
    if(r->pos != 0 && inOutputRule)
    {
      ret += compileClip(L"whole", r->pos, L"tl");
      ret += APPENDALLCHILDREN;
      ret += INT;
      ret += (wchar_t)r->pos;
      ret += GETRULE;
      ret += INT;
      ret += (wchar_t)0;
      ret += SETRULE;
    }
    if(inOutputRule && !r->nextConjoined)
    {
      ret += OUTPUT;
    }
  }
  else
  {
    ret += CHUNK;
    ret += compileString(r->lemma);
    ret += APPENDSURFACE;
    for(unsigned int i = 0; i < r->tags.size(); i++)
    {
      ret += compileClip(r->vars[r->tags[i]]);
      ret += APPENDSURFACE;
    }
    if(inOutputRule && !r->nextConjoined)
    {
      ret += OUTPUT;
    }
  }
  return ret;
}

wstring
RTXCompiler::processCond(Cond* cond)
{
  wstring ret;
  if(cond == NULL)
  {
    ret += PUSHTRUE;
    return ret;
  }
  if(cond->op == AND && (cond->left->op == 0 || cond->right->op == 0))
  {
    die(L"Cannot evaluate AND with string as operand (try adding parentheses).");
  }
  if(cond->op == OR && (cond->left->op == 0 || cond->right->op == 0))
  {
    die(L"Cannot evaluate OR with string as operand (try adding parentheses).");
  }
  if(cond->op == NOT && cond->right->op == 0)
  {
    die(L"Attempt to negate string value.");
  }
  if(cond->op == 0)
  {
    if(cond->val->src == 0)
    {
      ret = compileString(cond->val->part);
    }
    else
    {
      ret = compileClip(cond->val);
      if(cond->val->part != L"lem" && cond->val->part != L"lemh" && cond->val->part != L"lemq")
      {
        ret += DISTAG;
      }
    }
  }
  else if(cond->op == NOT)
  {
    ret = processCond(cond->right);
    ret += NOT;
  }
  else
  {
    ret = processCond(cond->left);
    ret += processCond(cond->right);
    ret += cond->op;
  }
  return ret;
}

wstring
RTXCompiler::processOutputChoice(OutputChoice* choice)
{
  wstring ret;
  if(choice->nest.back() == NULL)
  {
    ret += processOutputChunk(choice->chunks.back());
  }
  else
  {
    ret += processOutputChoice(choice->nest.back());
  }
  int n = choice->conds.size();
  for(int i = 1; i <= n; i++)
  {
    wstring act;
    if(choice->nest[n-i] != NULL)
    {
      act = processOutputChoice(choice->nest[n-i]);
    }
    else
    {
      act = processOutputChunk(choice->chunks[n-i]);
    }
    act += JUMP;
    act += (wchar_t)ret.size();
    wstring cond = processCond(choice->conds[n-i]);
    cond += JUMPONFALSE;
    cond += (wchar_t)act.size();
    ret = cond + act + ret;
  }
  return ret;
}

void
RTXCompiler::processRules()
{
  Rule* rule;
  for(unsigned int ruleid = 0; ruleid < reductionRules.size(); ruleid++)
  {
    rule = reductionRules[ruleid];
    currentRule = rule;
    currentChunk = NULL;
    currentChoice = NULL;
    makePattern(ruleid);
    wstring comp;
    if(rule->cond != NULL)
    {
      comp = processCond(rule->cond) + JUMPONTRUE + (wchar_t)1 + REJECTRULE;
    }
    vector<wstring> outcomp;
    outcomp.resize(rule->pattern.size());
    parentTags.clear();
    int patidx = 0;
    for(unsigned int i = 0; i < rule->output.size(); i++)
    {
      OutputChoice* cur = rule->output[i];
      if(cur->chunks.size() == 1 && cur->chunks[0]->mode == L"_")
      {
        comp += processOutputChoice(cur);
      }
      else if(cur->chunks.size() == 1 && cur->chunks[0]->mode == L"#")
      {
        comp += processOutputChoice(cur);
        patidx++;
      }
      else
      {
        OutputChunk* ch = new OutputChunk;
        ch->mode = L"#";
        ch->pos = 0;
        ch->getall = true;
        ch->vars = rule->vars;
        ch->conjoined = false;
        ch->nextConjoined = false;
        ch->pattern = rule->result[patidx];
        comp += processOutputChunk(ch);
        comp += INT;
        comp += (wchar_t)outputBytecode.size();
        comp += INT;
        comp += (wchar_t)0;
        comp += SETRULE;
        comp += APPENDALLINPUT;
        parentTags = outputRules[ch->pattern];
        inOutputRule = true;
        outputBytecode.push_back(processOutputChoice(cur));
        PB.outRuleNames.push_back(L"line " + to_wstring(rule->line));
        inOutputRule = false;
        parentTags.clear();
        patidx++;
      }
      comp += OUTPUT;
    }
    rule->compiled = comp;
  }
}

void
RTXCompiler::read(const string &fname)
{
  currentLine = 1;
  sourceFile = fname;
  source.open(fname);
  while(true)
  {
    eatSpaces();
    if(source.eof())
    {
      break;
    }
    parseRule();
  }
  source.close();
  errorsAreSyntax = false;
  processRules();
  for(map<wstring, vector<wstring>>::iterator it=collections.begin(); it != collections.end(); ++it)
  {
    set<wstring, Ltstr> vals;
    for(unsigned int i = 0; i < it->second.size(); i++)
    {
      vals.insert(it->second[i]);
    }
    PB.addList(it->first, vals);
    PB.addAttr(it->first, vals);
  }
}

void
RTXCompiler::write(const string &fname)
{
  FILE *out = fopen(fname.c_str(), "wb");
  if(!out)
  {
    cerr << "Error: cannot open '" << fname;
    cerr << "' for writing" << endl;
    exit(EXIT_FAILURE);
  }

  vector<PatternElement*> glue;
  set<wstring, Ltstr> seen;
  vector<pair<int, wstring>> inRules;
  for(unsigned int i = 0; i < reductionRules.size(); i++)
  {
    inRules.push_back(make_pair(2*reductionRules[i]->pattern.size() - 1,
                                reductionRules[i]->compiled));
    PB.inRuleNames.push_back(L"line " + to_wstring(reductionRules[i]->line));
    wstring tg = reductionRules[i]->result.back();
    if(seen.find(tg) == seen.end())
    {
      PatternElement* p = new PatternElement;
      p->tags.push_back(tg);
      p->tags.push_back(L"*");
      glue.push_back(p);
      seen.insert(tg);
    }
  }
  PatternElement* p = new PatternElement;
  p->tags.push_back(L"FALL:BACK");
  vector<vector<PatternElement*>> fb;
  fb.push_back(glue);
  fb.push_back(vector<PatternElement*>(1, p));
  PB.addPattern(fb, -1);

  PB.write(out, longestPattern, inRules, outputBytecode);

  fclose(out);
}
