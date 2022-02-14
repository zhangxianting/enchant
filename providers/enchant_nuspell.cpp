/* enchant
 * Copyright (C) 2022 Dimitrij Mijoski
 * Copyright (C) 2020 Sander van Geloven
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders
 * give permission to link the code of this program with
 * non-LGPL Spelling Provider libraries (eg: a MSFT Office
 * spell checker backend) and distribute linked combinations including
 * the two.  You must obey the GNU General Public License in all
 * respects for all of the code used other than said providers.  If you modify
 * this file, you may extend this exception to your version of the
 * file, but you are not obligated to do so.  If you do not wish to
 * do so, delete this exception statement from your version.
 */

/*
 * This is the Nuspell Enchant Backend.
 * Nuspell is by Dimitrij Mijoski and Sander van Geloven.
 * See: http://nuspell.github.io/
 */

#include "config.h"

#include <memory>

#include "enchant-provider.h"
#include "unused-parameter.h"

#include <nuspell/dictionary.hxx>
#include <nuspell/finder.hxx>

#include <glib.h>

using namespace std;

// EnchantDict functions
static int nuspell_dict_check(EnchantDict* me, const char* const word,
                              size_t len)
{
	auto dict = static_cast<nuspell::Dictionary*>(me->user_data);

	using UniquePtr = unique_ptr<char[], decltype(&g_free)>;
	auto normalized_word =
	    UniquePtr(g_utf8_normalize(word, len, G_NORMALIZE_NFC), g_free);
	return !dict->spell(normalized_word.get());
}

static char** nuspell_dict_suggest(EnchantDict* me, const char* const word,
                                   size_t len, size_t* out_n_suggs)
{
	auto dict = static_cast<nuspell::Dictionary*>(me->user_data);

	using UniquePtr = unique_ptr<char[], decltype(&g_free)>;
	// the 8-bit encodings use precomposed forms
	auto normalized_word =
	    UniquePtr(g_utf8_normalize(word, len, G_NORMALIZE_NFC), g_free);
	auto suggestions = vector<string>();
	dict->suggest(normalized_word.get(), suggestions);
	if (empty(suggestions)) {
		*out_n_suggs = 0;
		return nullptr;
	}
	char** sug_list = g_new0(char*, size(suggestions) + 1);
	transform(begin(suggestions), end(suggestions), sug_list,
	          [](const string& sug) { return g_strdup(sug.c_str()); });
	*out_n_suggs = size(suggestions);
	return sug_list;
}
// End EnchantDict functions

// EnchantProvider functions
static void nuspell_provider_dispose(EnchantProvider* me) { g_free(me); }

static EnchantDict*
nuspell_provider_request_dict([[maybe_unused]] EnchantProvider* me,
                              const char* const tag)
{
	auto dirs = vector<filesystem::path>();
	nuspell::append_default_dir_paths(dirs);
	auto dic_path = nuspell::search_dirs_for_one_dict(dirs, tag);
	if (empty(dic_path))
		return nullptr;

	auto dict_cpp = make_unique<nuspell::Dictionary>();
	try {
		dict_cpp->load_aff_dic(dic_path);
	}
	catch (const nuspell::Dictionary_Loading_Error&) {
		return nullptr;
	}

	EnchantDict* dict = g_new0(EnchantDict, 1);
	dict->user_data = static_cast<void*>(dict_cpp.release());
	dict->check = nuspell_dict_check;
	dict->suggest = nuspell_dict_suggest;
	return dict;
}

static void nuspell_provider_dispose_dict([[maybe_unused]] EnchantProvider* me,
                                          EnchantDict* dict)
{
	auto dict_cpp = static_cast<nuspell::Dictionary*>(dict->user_data);
	delete dict_cpp;
	g_free(dict);
}

static int
nuspell_provider_dictionary_exists([[maybe_unused]] EnchantProvider* me,
                                   const char* const tag)
{
	auto dirs = vector<filesystem::path>();
	nuspell::append_default_dir_paths(dirs);
	auto dic_path = nuspell::search_dirs_for_one_dict(dirs, tag);
	return !empty(dic_path);
}

static const char*
nuspell_provider_identify([[maybe_unused]] EnchantProvider* me)
{
	return "nuspell";
}

static const char*
nuspell_provider_describe([[maybe_unused]] EnchantProvider* me)
{
	return "Nuspell Provider";
}

static char**
nuspell_provider_list_dicts(EnchantProvider* me _GL_UNUSED_PARAMETER,
                            size_t* out_n_dicts)
{
	auto dicts = nuspell::search_default_dirs_for_dicts();
	if (empty(dicts)) {
		*out_n_dicts = 0;
		return nullptr;
	}
	for (auto& d : dicts)
		d = d.stem();
	sort(begin(dicts), end(dicts));
	auto it = unique(begin(dicts), end(dicts));
	it = remove_if(begin(dicts), it, [](const filesystem::path& p) {
		auto& n = p.native();
		return any_of(begin(n), end(n),
		              [](auto c) { return c < 0 || c > 127; });
	});
	dicts.erase(it, end(dicts));

	char** dictionary_list = g_new0(char*, size(dicts) + 1);
	transform(begin(dicts), end(dicts), dictionary_list,
	          [](const filesystem::path& p) {
		          return g_strdup(p.string().c_str());
	          });
	*out_n_dicts = size(dicts);
	return dictionary_list;
}

extern "C" EnchantProvider* init_enchant_provider(void);

EnchantProvider *
init_enchant_provider (void)
{
	EnchantProvider *provider = g_new0(EnchantProvider, 1);
	provider->dispose = nuspell_provider_dispose;
	provider->request_dict = nuspell_provider_request_dict;
	provider->dispose_dict = nuspell_provider_dispose_dict;
	provider->dictionary_exists = nuspell_provider_dictionary_exists;
	provider->identify = nuspell_provider_identify;
	provider->describe = nuspell_provider_describe;
	provider->list_dicts = nuspell_provider_list_dicts;

	return provider;
}
