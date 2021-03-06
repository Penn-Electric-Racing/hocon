#include <internal/parseable.hpp>
#include <sstream>
#include <internal/nodes/abstract_config_node.hpp>
#include <internal/nodes/config_node_object.hpp>
#include <internal/simple_config_document.hpp>
#include <internal/values/simple_config_object.hpp>
#include <hocon/config_exception.hpp>
#include <internal/tokenizer.hpp>
#include <internal/simple_includer.hpp>
#include <internal/config_document_parser.hpp>
#include <internal/simple_include_context.hpp>
#include <internal/config_parser.hpp>
#include <vector>
#include <numeric>
#include <fstream>

using namespace std;


// replace boost
static bool ends_with(std::string const &fullString, std::string const &ending) {
    if (fullString.length() >= ending.length()) {
        return (0 == fullString.compare (fullString.length() - ending.length(), ending.length(), ending));
    } else {
        return false;
    }
}

thread_local std::map< void*, std::shared_ptr< void > > thread_specific_ptr_data;

template< typename T >
class thread_specific_ptr {
public:
    T* get() const
    {
        auto it = thread_specific_ptr_data.find((void*)this);
        if (it != thread_specific_ptr_data.end())
            return static_cast< T* >(it->second.get());
        return nullptr;
    }
    void reset(T* v) {
        thread_specific_ptr_data[this].reset(v);
    }
    void reset() {
        thread_specific_ptr_data[this].reset();
    }
};

class scope_exit {
public:
    scope_exit(const std::function<void()>& f) : f_(f) {}
    ~scope_exit() { f_(); }
private:
    std::function<void()> f_;
};

namespace hocon {

    const int parseable::MAX_INCLUDE_DEPTH = 50;

    shared_ptr<parseable> parseable::new_file(std::string input_file_path, config_parse_options options) {
        return make_shared<parseable_file>(move(input_file_path),  move(options));
    }

    shared_ptr<parseable> parseable::new_string(std::string s, config_parse_options options) {
        return make_shared<parseable_string>(move(s), move(options));
    }

    shared_ptr<parseable> parseable::new_not_found(std::string what_not_found, std::string message,
                                                 config_parse_options options) {
        return make_shared<parseable_not_found>(move(what_not_found), move(message), move(options));
    }

    void parseable::post_construct(config_parse_options const& base_options) {
        _initial_options = fixup_options(move(base_options));

        _include_context = make_shared<simple_include_context>(*this);

        if (_initial_options.get_origin_description()) {
            _initial_origin = make_shared<simple_config_origin>(*_initial_options.get_origin_description());
        } else {
            _initial_origin = create_origin();
        }
    }

    config_syntax parseable::syntax_from_extension(std::string name) {
        if (ends_with(name, ".json")) {
            return config_syntax::JSON;
        } else if (ends_with(name, ".conf")) {
            return config_syntax::CONF;
        } else {
            return config_syntax::UNSPECIFIED;
        }
    }

    config_parse_options const& parseable::options() const {
        return _initial_options;
    }

    shared_ptr<const config_origin> parseable::origin() const {
        return _initial_origin;
    }

    config_parse_options parseable::fixup_options(config_parse_options const& base_options) const {
        config_syntax syntax = base_options.get_syntax();
        if (syntax == config_syntax::UNSPECIFIED) {
            syntax = guess_syntax();
        }
        if (syntax == config_syntax::UNSPECIFIED) {
            syntax = config_syntax::CONF;
        }
        config_parse_options modified = base_options.set_syntax(syntax);

        // make sure the app-provided includer falls back to default
        modified = modified.append_includer(config::default_includer());
        // make sure the app-provided includer is complete
        modified = modified.set_includer(simple_includer::make_full(modified.get_includer()));

        return modified;
    }

    config_syntax parseable::guess_syntax() const {
        return config_syntax::UNSPECIFIED;
    }

    config_syntax parseable::content_type() const {
        return config_syntax::UNSPECIFIED;
    }

    shared_ptr<config_parseable> parseable::relative_to(string file_name) const {
        // we don't have classpath or resoucespath in c++. (it does in Java)
        // we treat the "filename" as absolute or relative path
        // to a specific hocon file. (URL is not supported now)
        // if it starts with "/", we consider it as a absolute path.
        // otherwise, it's a relative path.
        string resource = file_name;
        if (!file_name.empty() && '/' == file_name[0]) {
            resource = file_name;
        } else {
            resource = get_cur_dir() + file_name;
        }
        return parseable::new_file(move(resource), _include_context->parse_options());
    }

    string parseable::to_string() const {
        return typeid(*this).name();
    }

    void parseable::set_cur_dir(std::string dir) const {
        _include_context->set_cur_dir(move(dir));
    }

    std::string parseable::get_cur_dir() const {
        return _include_context->get_cur_dir();
    }

    void parseable::separate_filepath(const std::string& path, std::string* file_dir, std::string* file_name) const {
        char sep = '/';
        size_t i = path.rfind(sep, path.length());
        if (std::string::npos != i) {
            file_dir->assign(path.substr(0, i + 1));
            file_name->assign(path.substr(i + 1, path.length() - i));
        } else {
            file_dir->assign("");
            file_name->assign(path);
        }
    }

    shared_ptr<config_document> parseable::parse_config_document() {
        return parse_document(_initial_options);
    }

    static shared_object force_parsed_to_object(shared_value value) {
        if (auto obj = dynamic_pointer_cast<const config_object>(value)) {
            return obj;
        } else {
            throw wrong_type_exception(*value->origin(), "", "object at file root", value->value_type_name());
        }
    }

    shared_object parseable::parse(config_parse_options const& options) const {
        static thread_specific_ptr<vector<shared_ptr<const parseable>>> parse_stack;
        if (!parse_stack.get()) {
            // Initialize the stack
            parse_stack.reset(new vector<shared_ptr<const parseable>>());
        }

        auto pstack = parse_stack.get();
        if (pstack->size() >= MAX_INCLUDE_DEPTH) {
            string stacktrace = accumulate(pstack->begin(), pstack->end(), string(), [](string s, shared_ptr<const parseable> p) {
                return s + '\t' + p->to_string() + '\n';
            });
            throw parse_exception(*_initial_origin, "include statements nested more than " + std::to_string(MAX_INCLUDE_DEPTH) + " times, you probably have a cycle in your includes. Trace:\n" + stacktrace);
        }

        pstack->push_back(shared_from_this());
        // call after return when exiting scope
        scope_exit scope([&]() {
            pstack->pop_back();
            if (pstack->empty()) {
                parse_stack.reset();
            }
        });

        return force_parsed_to_object(parse_value(options));
    }

    shared_object parseable::parse() const {
        return force_parsed_to_object(parse_value(config_parse_options()));
    }

    shared_value parseable::parse_value() const {
        return parse_value(options());
    }

    shared_value parseable::parse_value(config_parse_options const& base_options) const {
        auto options = fixup_options(base_options);

        // passed-in options can override origin
        shared_origin origin = options.get_origin_description() ?
                               make_shared<simple_config_origin>(*options.get_origin_description()) :
                               _initial_origin;
        return parse_value(move(origin), move(options));
    }

    shared_value parseable::parse_value(shared_origin origin, config_parse_options const& final_options) const {
        try {
            return raw_parse_value(origin, final_options);
        } catch (const runtime_error& e) {
            if (final_options.get_allow_missing()) {
                return make_shared<simple_config_object>(
                        make_shared<simple_config_origin>(origin->description() + " (not found)"),
                        unordered_map<string, shared_value>());
            } else {
                throw io_exception(*origin, e.what());
            }
        }
    }

    shared_value parseable::raw_parse_value(shared_origin origin, config_parse_options const& options) const {
        auto stream = reader(options);

        // after reader() we will have loaded the content type
        config_syntax cont_type = content_type();
        config_parse_options options_with_content_type;
        if (cont_type != config_syntax::UNSPECIFIED) {
            options_with_content_type = options.set_syntax(cont_type);
        } else {
            options_with_content_type = options;
        }

        return raw_parse_value(move(stream), origin, options_with_content_type);
    }

    shared_value parseable::raw_parse_value(unique_ptr<istream> stream, shared_origin origin,
                                            config_parse_options const& options) const {
        // config_syntax::PROPERTIES handling not needed because we don't plan to support it.
        token_iterator tokens(origin, move(stream), options.get_syntax());
        auto document = config_document_parser::parse(move(tokens), origin, options);
        return config_parser::parse(document, origin, options, _include_context);
    }

    shared_ptr<config_document> parseable::parse_document(config_parse_options const& base_options) const {
        // note that we are NOT using our "_initial_options",
        // but using the ones from the passed-in options. The idea is that
        // callers can get our original options and then parse with different
        // ones if they want.
        config_parse_options options = fixup_options(base_options);

        // passed in options can override origin
        shared_origin origin = _initial_origin;
        if (options.get_origin_description()) {
            origin = make_shared<simple_config_origin>(*options.get_origin_description());
        }
        return parse_document(origin, options);
    }

    std::shared_ptr<config_document> parseable::parse_document(shared_origin origin,
                                                               config_parse_options const& final_options) const {
        try {
            return raw_parse_document(origin, final_options);
        } catch (const runtime_error& e) {
            if (final_options.get_allow_missing()) {
                shared_node_list children;
                children.push_back(make_shared<config_node_object>(shared_node_list { }));
                return make_shared<simple_config_document>(make_shared<config_node_root>(children, origin),
                                                           final_options);
            } else {
                throw config_exception("exception loading " + origin->description() + ": " + e.what());
            }
        }
    }

    std::shared_ptr<config_document> parseable::raw_parse_document(shared_origin origin,
                                                                   config_parse_options const& options) const {
        auto stream = reader(options);

        config_syntax cont_type = content_type();

        config_parse_options options_with_content_type;
        if (cont_type != config_syntax::UNSPECIFIED) {
            options_with_content_type = options.set_syntax(cont_type);
        } else {
            options_with_content_type = options;
        }

        return raw_parse_document(move(stream), move(origin), options_with_content_type);
    }

    std::shared_ptr<config_document> parseable::raw_parse_document(std::unique_ptr<std::istream> stream,
                                                                   shared_origin origin,
                                                                   config_parse_options const& options) const {
        auto tokens = token_iterator(origin, move(stream), options.get_syntax());
        return make_shared<simple_config_document>(config_document_parser::parse(move(tokens), origin, options), options);
    }

    unique_ptr<istream> parseable::reader(config_parse_options const& options) const {
        return reader();
    }

    /** Parseable file */
    parseable_file::parseable_file(std::string input_file_path, config_parse_options options) :
        _input(move(input_file_path)) {
        post_construct(options);
        string dir, name;
        separate_filepath(_input, &dir, &name);
        set_cur_dir(dir);
    }

    unique_ptr<istream> parseable_file::reader() const {
        std::ifstream *is = new std::ifstream();
        is->open(_input.c_str());
        if (!is->is_open()) throw runtime_error("not found");
        return unique_ptr<istream>(is);
    }

    shared_origin parseable_file::create_origin() const {
        return make_shared<simple_config_origin>("file: " + _input);
    }

    config_syntax parseable_file::guess_syntax() const {
        return syntax_from_extension(_input);
    }

    /** Parseable string */
    parseable_string::parseable_string(std::string s, config_parse_options options) : _input(move(s)) {
        post_construct(options);
    }

    unique_ptr<istream> parseable_string::reader() const {
        return unique_ptr<istringstream>(new istringstream(_input));
    }

    shared_origin parseable_string::create_origin() const {
        return make_shared<simple_config_origin>("string");
    }

    /** Parseable resources */
    parseable_resources::parseable_resources(std::string resource, config_parse_options options) :
            _resource(move(resource)) {
        post_construct(options);
    }

    std::unique_ptr<std::istream> parseable_resources::reader() const {
        throw config_exception("reader() should not be called on resources");
    }

    shared_origin parseable_resources::create_origin() const {
        return make_shared<simple_config_origin>(_resource);
    }

    /** Parseable Not Found */
    parseable_not_found::parseable_not_found(std::string what, std::string message, config_parse_options options) :
            _what(move(what)), _message(move(message)) {
        post_construct(options);
    }

    std::unique_ptr<std::istream> parseable_not_found::reader() const {
        throw config_exception(_message);
    }

    shared_origin parseable_not_found::create_origin() const {
        return make_shared<simple_config_origin>(_what);
    }
}  // namespace hocon
