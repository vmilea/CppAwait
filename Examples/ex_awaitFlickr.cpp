/*
* Copyright 2012-2015 Valentin Milea
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#ifdef HAVE_OPENSSL

#include "ExUtil.h"
#include <CppAwait/Awaitable.h>
#include <CppAwait/AsioWrappers.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/xml_parser.hpp>

//
// ABOUT: fetch pictures from Flickr
//

using namespace boost::asio::ip;
namespace ssl = boost::asio::ssl;

// run loop
static boost::asio::io_service sIo;

//
// flickr utils
//

static const std::string FLICKR_API_HOST = "api.flickr.com";
static const std::string FLICKR_API_KEY = "e36784df8a03fea04c22ed93318b291c";

struct FlickrPhoto
{
    std::string id;
    std::string owner;
    std::string secret;
    std::string server;
    std::string farm;
    std::string title;
};

struct FlickrPhotos
{
    int page;
    int pages;
    int perPage;
    int total;
    std::vector<FlickrPhoto> photos;
};

static FlickrPhotos parseFlickrResponse(boost::asio::streambuf& response)
{
    namespace pt = boost::property_tree;

    FlickrPhotos result;

    std::stringstream ss;
    ss << &response;
    pt::ptree ptree;
    pt::read_xml(ss, ptree);

    const pt::ptree& resp = ptree.get_child("rsp");
    const std::string& stat = resp.get("<xmlattr>.stat", "error");

    if (stat != "ok") {
        printf ("\nbad response:\n%s\n", ss.str().c_str());
        throw std::runtime_error("flickr response not ok");
    }

    foreach_(const auto& node, resp.get_child("photos")) {
        const pt::ptree& value = node.second;

        if (node.first == "<xmlattr>") {
            result.page = value.get<int>("page");
            result.pages = value.get<int>("pages");
            result.perPage = value.get<int>("perpage");
            result.total = value.get<int>("total");
        } else {
            FlickrPhoto fp;
            fp.id = value.get<std::string>("<xmlattr>.id");
            fp.owner = value.get<std::string>("<xmlattr>.owner");
            fp.secret = value.get<std::string>("<xmlattr>.secret");
            fp.server = value.get<std::string>("<xmlattr>.server");
            fp.farm = value.get<std::string>("<xmlattr>.farm");
            fp.title = value.get<std::string>("<xmlattr>.title");

            result.photos.push_back(fp);
        }
    }

    return result;
}

static std::pair<std::string, std::string> makeFlickrQueryUrl(const std::vector<std::string>& tags, int perPage, int page)
{
    std::string path = "/services/rest/?method=flickr.photos.search&format=rest&api_key=" + FLICKR_API_KEY;

    // std::string path = "api.flickr.com/services/rest/?method=flickr.photos.search&format=rest&api_key=e36784df8a03fea04c22ed93318b291c&tags=kitten
    //  https://api.flickr.com/services/rest/?method=flickr.test.echo&name=value&api_key=e36784df8a03fea04c22ed93318b291c

    path += "&tags=" + tags[0];
    for (size_t i = 1; i < tags.size(); i++) {
        path += "+" + tags[i];
    }

    path += "&per_page=" + boost::lexical_cast<std::string>(perPage);
    path += "&page=" + boost::lexical_cast<std::string>(page);

    return std::make_pair(FLICKR_API_HOST, path);
}

static std::pair<std::string, std::string> makeFlickrPhotoUrl(const FlickrPhoto& photo)
{
    // format: http://farm{farm-id}.staticflickr.com/{server-id}/{id}_{secret}_[mstzb].jpg

    std::string host = "farm" + photo.farm + ".staticflickr.com";
    std::string path = "/" + photo.server + "/" + photo.id + "_" + photo.secret + "_m.jpg";

    return std::make_pair(host, path);
}

static ut::Awaitable asyncFlickrDownload(const std::vector<std::string>& tags, int numPics, int numPicsPerPage)
{
    static const int MAX_PARALLEL_DOWNLOADS = 6;

    return ut::startAsync("asyncFlickrDownload", [tags, numPics, numPicsPerPage]() {
        struct DownloadSlot
        {
            std::unique_ptr<ut::Awaitable> awaitable;
            std::shared_ptr<boost::asio::streambuf> buf;
            FlickrPhoto *photo;

            DownloadSlot()
                : buf(std::make_shared<boost::asio::streambuf>())
                , photo(nullptr) { }
        };

        typedef std::array<DownloadSlot, MAX_PARALLEL_DOWNLOADS> DownloadSlots;

        try {
            int totalPicsRemaining = numPics;
            int page = 1;

            DownloadSlots dlslots;
            int numSlotsUsed = 0;

            auto startFetch = [&](DownloadSlots::iterator pos, FlickrPhoto& photo) {
                assert (pos->photo == nullptr); // slot must be available

                auto photoUrl = makeFlickrPhotoUrl(photo);
                printf (" fetching %s%s ...\n", photoUrl.first.c_str(), photoUrl.second.c_str());

                pos->awaitable = ut::asUniquePtr(ut::asio::asyncHttpDownload(sIo, photoUrl.first, photoUrl.second, pos->buf));
                pos->photo = &photo;

                numSlotsUsed++;
                totalPicsRemaining--;
            };

            // prepare a persistent SSL connection for API queries
            typedef ssl::stream<tcp::socket> SslSocket;
            ssl::context ctx(ssl::context::sslv3_client);
            SslSocket apiSocket(sIo, ctx);
            // connect
            tcp::resolver::query query(FLICKR_API_HOST, "https");
            tcp::resolver::iterator itConnected;
            ut::Awaitable awt = ut::asio::asyncResolveAndConnect(apiSocket.lowest_layer(), query, itConnected);
            awt.await();
            apiSocket.lowest_layer().set_option(tcp::no_delay(true));
            apiSocket.lowest_layer().set_option(boost::asio::socket_base::keep_alive(true));
            // perform SSL handshake
            awt = ut::asio::asyncHandshake(apiSocket, SslSocket::client);
            awt.await();

            while (totalPicsRemaining > 0) {
                // download a page
                auto queryUrl = makeFlickrQueryUrl(tags, numPicsPerPage, page);
                auto response = std::make_shared<boost::asio::streambuf>();
                // read HTTP header
                size_t contentLength;
                awt = ut::asio::asyncHttpGet(apiSocket, queryUrl.first, queryUrl.second, true, response, contentLength);
                awt.await();

                // parse xml
                FlickrPhotos resp = parseFlickrResponse(*response);

                printf ("query result: %ld photos, page %d/%d, %d per page, %d total\n",
                    (long) resp.photos.size(), resp.page, resp.pages, resp.perPage, resp.total);

                int availablePicsRemaining = resp.total - (resp.page - 1) * resp.perPage;
                totalPicsRemaining = std::min(totalPicsRemaining, availablePicsRemaining);

                auto itPagePhotos = resp.photos.begin();

                // download page photos in parallel (up to MAX_PARALLEL_DOWNLOADS)
                for (auto it = dlslots.begin(); it != dlslots.end(); ++it) {
                    if (itPagePhotos == resp.photos.end() || totalPicsRemaining == 0) {
                        break;
                    }
                    startFetch(it, *itPagePhotos++);
                }

                // advance as slots gets freed
                while (numSlotsUsed > 0) {
                    DownloadSlots::iterator pos = ut::awaitAny(dlslots);
                    pos->awaitable->await(); // won't yield again, just check for exception

                    std::string savePath = pos->photo->id + ".jpg";
                    std::ofstream fout(savePath, std::ios::binary);
                    fout << pos->buf.get();
                    printf ("  saved %s\n", savePath.c_str());

                    // release slot
                    pos->awaitable = nullptr;
                    pos->photo = nullptr;
                    numSlotsUsed--;

                    if (itPagePhotos != resp.photos.end() && totalPicsRemaining > 0) {
                        startFetch(pos, *itPagePhotos++);
                    }
                }

                page++;
            }
        } catch (std::exception& e) { // exceptions get propagated into awaiting coroutine
            printf ("Download failed: %s - %s\n", typeid(e).name(), e.what());
        } catch (...) {
            printf ("Download failed: unknown exception\n");
        }
    }, 256 * 1024); // need stack > 64KB
}

void ex_awaitFlickr()
{
    printf ("tags (default 'kitten'): ");
    std::string tags = readLine();

    std::vector<std::string> splitTags;
    boost::split(splitTags, tags, boost::is_space());
    splitTags.resize(
        std::remove(splitTags.begin(), splitTags.end(), "") - splitTags.begin());

    if (splitTags.empty()) {
        splitTags.push_back("kitten");
    }

    ut::Awaitable awt = asyncFlickrDownload(splitTags, 25, 10);

    // loops until all async handlers have ben dispatched
    sIo.run();
}

#endif // HAVE_OPENSSL
